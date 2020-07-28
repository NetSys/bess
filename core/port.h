// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_PORT_H_
#define BESS_PORT_H_

#include <glog/logging.h>
#include <google/protobuf/any.pb.h>
#include <gtest/gtest_prod.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "message.h"
#include "module.h"
#include "packet.h"
#include "pb/port_msg.pb.h"
#include "pktbatch.h"
#include "utils/common.h"
#include "utils/ether.h"

using queue_t = uint8_t;

#define MAX_QUEUES_PER_DIR 128 /* [0, 127] (for each RX/TX) */

#define MAX_QUEUE_SIZE 4096

#define ETH_ALEN 6

// The term RX/TX could be very confusing for a virtual switch.
// Instead, we use the "incoming/outgoing" convention:
// - incoming: outside -> BESS
// - outgoing: BESS -> outside
//
// NOTE: for new code, avoid using arrays like `Foo bar[PACKET_DIR]`.
//   The use of array doesn't really make the code more readable.
//   Instead, follow the `Foo bar_inc; Foo bar_out` style.
typedef enum {
  PACKET_DIR_INC = 0,
  PACKET_DIR_OUT = 1,
  PACKET_DIRS
} packet_dir_t;

class Port;
class PortTest;

using port_init_func_t =
    pb_func_t<CommandResponse, Port, google::protobuf::Any>;

template <typename T, typename P>
static inline port_init_func_t PORT_INIT_FUNC(
    CommandResponse (P::*fn)(const T &)) {
  return [fn](Port *p, const google::protobuf::Any &arg) {
    T arg_;
    arg.UnpackTo(&arg_);
    auto base_fn = std::mem_fn(fn);
    return base_fn(static_cast<P *>(p), arg_);
  };
}

// A class to generate new Port objects of specific types.  Each instance can
// generate Port objects of a specific class and specification.  Represents a
// "driver" of that port.
class PortBuilder {
 public:
  friend class PortTest;
  friend class ZeroCopyVPortTest;
  friend class PortBuilderTest;

  PortBuilder(std::function<Port *()> port_generator,
              const std::string &class_name, const std::string &name_template,
              const std::string &help_text, port_init_func_t init_func)
      : port_generator_(port_generator),
        class_name_(class_name),
        name_template_(name_template),
        help_text_(help_text),
        init_func_(init_func),
        initialized_(false) {}

  // Returns a new Port object of the type represented by this PortBuilder
  // instance (of type class_name) with the Port instance's name set to the
  // given name.
  Port *CreatePort(const std::string &name) const;

  // Adds the given Port to the global Port collection.  Takes ownership of the
  // pointer.  Returns true upon success.
  static bool AddPort(Port *p);

  // Returns 0 upon success, -errno upon failure.
  static int DestroyPort(Port *p);

  // Generates a name for a new port given the driver name and its template.
  static std::string GenerateDefaultPortName(
      const std::string &driver_name, const std::string &default_template);

  // Invokes one-time initialization of the corresponding port class.  Returns
  // true upon success.
  bool InitPortClass();

  // Initializes all drivers.
  static void InitDrivers();

  // Should be called via ADD_DRIVER (once per driver file) to register the
  // existence of this driver.  Always returns true;
  static bool RegisterPortClass(std::function<Port *()> port_generator,
                                const std::string &class_name,
                                const std::string &name_template,
                                const std::string &help_text,
                                port_init_func_t init_func);

  static const std::map<std::string, PortBuilder> &all_port_builders();

  static const std::map<std::string, Port *> &all_ports();

  const std::string &class_name() const { return class_name_; }
  const std::string &name_template() const { return name_template_; }
  const std::string &help_text() const { return help_text_; }
  bool initialized() const { return initialized_; }

  CommandResponse RunInit(Port *p, const google::protobuf::Any &arg) const {
    return init_func_(p, arg);
  }

 private:
  // To avoid the static initialization ordering problem, this pseudo-getter
  // function contains the real static all_port_builders class variable and
  // returns it, ensuring its construction before use.
  //
  // If reset is true, clears the store of all port builders; to be used for
  // testing and for dynamic loading of "drivers".
  static std::map<std::string, PortBuilder> &all_port_builders_holder(
      bool reset = false);

  // A function that emits a new Port object of the type class_name.
  std::function<Port *()> port_generator_;

  // Tracks all port instances.
  static std::map<std::string, Port *> all_ports_;

  std::string class_name_;     // The name of this Port class.
  std::string name_template_;  // The port default name prefix.
  std::string help_text_;      // Help text about this port type.

  port_init_func_t init_func_;  // Initialization function of this Port class

  bool initialized_;  // Has this port class been initialized via
                      // InitPortClass()?
};

class Port {
 public:
  struct LinkStatus {
    uint32_t speed;    // speed in mbps: 1000, 40000, etc. 0 for vports
    bool full_duplex;  // full-duplex enabled?
    bool autoneg;      // auto-negotiated speed and duplex?
    bool link_up;      // link up?
  };

  struct Conf {
    bess::utils::Ethernet::Address mac_addr;
    uint32_t mtu;
    bool admin_up;
  };

  struct QueueStats {
    uint64_t packets;
    uint64_t dropped;  // Not all drivers support this for INC direction
    uint64_t bytes;    // It doesn't include Ethernet overhead
  };

  struct PortStats {
    QueueStats inc;
    QueueStats out;

    // Per-queue stat counters. The sum of all queues should match the above `inc`.
    // Key -1 is used when exact queues are unknown.
    std::map<int, Port::QueueStats> inc_queues;
    std::map<int, Port::QueueStats> out_queues;
  };

  struct DriverFeatures {
    bool offloadIncStats;
    bool offloadOutStats;
  };

  // overide this section to create a new driver -----------------------------
  Port()
      : port_stats_(),
        queue_stats_(),
        conf_(),
        name_(),
        driver_arg_(),
        port_builder_(),
        num_queues(),
        queue_size(),
        users() {
    conf_.mac_addr.Randomize();
    conf_.mtu = kDefaultMtu;
    conf_.admin_up = true;
  }

  virtual ~Port() {}

  virtual void DeInit() = 0;

  // For one-time initialization of the port's "driver" (optional).
  virtual void InitDriver() {}

  virtual void CollectStats(bool reset);

  virtual int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) = 0;
  virtual int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) = 0;

  // For custom incoming / outgoing queue sizes (optional).
  virtual size_t DefaultIncQueueSize() const { return kDefaultIncQueueSize; }
  virtual size_t DefaultOutQueueSize() const { return kDefaultOutQueueSize; }

  virtual DriverFeatures GetFeatures() const { return {}; }

  /*!
   * Get any placement constraints that need to be met when receiving from this
   * port.
   */
  virtual placement_constraint GetNodePlacementConstraint() const {
    return UNCONSTRAINED_SOCKET;
  }

  virtual LinkStatus GetLinkStatus() {
    return LinkStatus{
        .speed = 0,
        .full_duplex = true,
        .autoneg = true,
        .link_up = true,
    };
  }

  virtual CommandResponse UpdateConf(const Conf &) {
    return CommandFailure(ENOTSUP);
  }

  CommandResponse InitWithGenericArg(const google::protobuf::Any &arg);

  PortStats GetPortStats();
  std::map<int, QueueStats> GetQueueStats();

  /* queues == nullptr if _all_ queues are being acquired/released */
  int AcquireQueues(const struct module *m, packet_dir_t dir,
                    const queue_t *queues, int num);

  void ReleaseQueues(const struct module *m, packet_dir_t dir,
                     const queue_t *queues, int num);

  const std::string &name() const { return name_; }
  const Conf &conf() const { return conf_; }
  const google::protobuf::Any &driver_arg() const { return driver_arg_; }

  uint64_t num_rx_queues() const { return num_queues[PACKET_DIR_INC]; }
  uint64_t num_tx_queues() const { return num_queues[PACKET_DIR_OUT]; }

  uint64_t rx_queue_size() const { return queue_size[PACKET_DIR_INC]; }
  uint64_t tx_queue_size() const { return queue_size[PACKET_DIR_OUT]; }

  const PortBuilder *port_builder() const { return port_builder_; }

  void IncreaseIncQueueCounters(queue_t qid, uint64_t received,
                                uint64_t dropped, uint64_t received_bytes) {
    auto &qstats = queue_stats_[PACKET_DIR_INC][qid];
    qstats.packets += received;
    qstats.dropped += dropped;
    qstats.bytes += received_bytes;
  }

  void IncreaseOutQueueCounters(queue_t qid, uint64_t sent, uint64_t dropped,
                                uint64_t sent_bytes) {
    auto &qstats = queue_stats_[PACKET_DIR_OUT][qid];
    qstats.packets += sent;
    qstats.dropped += dropped;
    qstats.bytes += sent_bytes;
  }

 protected:
  friend class PortBuilder;

  // for stats that do NOT counted for any per-queue stats below
  PortStats port_stats_;

  // per-queue stat counters
  QueueStats queue_stats_[PACKET_DIRS][MAX_QUEUES_PER_DIR];

  // Current configuration
  Conf conf_;

 private:
  static const size_t kDefaultIncQueueSize = 1024;
  static const size_t kDefaultOutQueueSize = 1024;

  static const uint32_t kDefaultMtu = 1500;

  // Private methods, for use by PortBuilder.
  void set_name(const std::string &name) { name_ = name; }
  void set_port_builder(const PortBuilder *port_builder) {
    port_builder_ = port_builder;
  }

  std::string name_;                  // The name of this port instance.
  google::protobuf::Any driver_arg_;  // Driver specific configuration.

  // Class-wide spec of this type of port.  Non-owning.
  const PortBuilder *port_builder_;

  DISALLOW_COPY_AND_ASSIGN(Port);

  // FIXME: porting in progress ----------------------------
 public:
  queue_t num_queues[PACKET_DIRS];
  size_t queue_size[PACKET_DIRS];

  /* which modules are using this port?
   * TODO: more robust gate keeping */
  const struct module *users[PACKET_DIRS][MAX_QUEUES_PER_DIR];
};

#define ADD_DRIVER(_DRIVER, _NAME_TEMPLATE, _HELP)                       \
  bool __driver__##_DRIVER = PortBuilder::RegisterPortClass(             \
      std::function<Port *()>([]() { return new _DRIVER(); }), #_DRIVER, \
      _NAME_TEMPLATE, _HELP, PORT_INIT_FUNC(&_DRIVER::Init));

#endif  // BESS_PORT_H_
