#ifndef _PORT_H_
#define _PORT_H_

#include <stdint.h>

#include <functional>
#include <memory>
#include <map>

#include <glog/logging.h>

#include "common.h"
#include "snobj.h"
#include "snbuf.h"

typedef uint8_t queue_t;

#define QUEUE_UNKNOWN 255
#define MAX_QUEUES_PER_DIR 32 /* [0, 31] (for each RX/TX) */

static_assert(MAX_QUEUES_PER_DIR < QUEUE_UNKNOWN, "too many queues");

#define DRIVER_FLAG_SELF_INC_STATS 0x0001
#define DRIVER_FLAG_SELF_OUT_STATS 0x0002

#define PORT_NAME_LEN 128

#define MAX_QUEUE_SIZE 4096

#define ETH_ALEN 6

/* The term RX/TX could be very confusing for a virtual switch.
 * Instead, we use the "incoming/outgoing" convention:
 * - incoming: outside -> BESS
 * - outgoing: BESS -> outside */
typedef enum {
  PACKET_DIR_INC = 0,
  PACKET_DIR_OUT = 1,
  PACKET_DIRS
} packet_dir_t;

struct packet_stats {
  uint64_t packets;
  uint64_t dropped; /* Not all drivers support this for inc dir */
  uint64_t bytes;   /* doesn't include Ethernet overhead */
};

typedef struct packet_stats port_stats_t[PACKET_DIRS];

class Port;

// A class to generate new Port objects of specific types.  Each instance can
// generate Port objects of a specific class and specification.  Represents a
// "driver" of that port.
class PortBuilder {
 public:
  PortBuilder(std::function<Port *()> port_generator,
              const std::string &class_name,
              const std::string &name_template,
              const std::string &help_text) :
      port_generator_(port_generator),
      class_name_(class_name),
      name_template_(name_template),
      help_text_(help_text),
      initialized_(false) {}

  // Returns a new Port object of the type represented by this PortBuilder
  // instance (of type class_name) with the Port instance's name set to the
  // given name.
  Port *CreatePort(const std::string &name) const;

  // Adds the given Port to the global Port collection.  Returns true upon
  // success.
  static bool AddPort(Port *p);

  // Returns 0 upon success, -errno upon failure.
  static int DestroyPort(Port *p);

  // Generates a name for a new port given the driver name and its template.
  static std::string GenerateDefaultPortName(const std::string &driver_name,
                                             const std::string &default_template);

  // Invokes one-time initialization of the corresponding port class.  Returns
  // true upon success.
  bool InitPortClass();

  // Should be called via ADD_DRIVER (once per driver file) to register the
  // existence of this driver.  Always returns true;
  static bool RegisterPortClass(std::function<Port *()> port_generator,
                                const std::string &class_name,
                                const std::string &name_template,
                                const std::string &help_text);

  static const std::map<std::string, PortBuilder> &all_port_builders();

  static const std::map<std::string, Port*> &all_ports();
  
  const std::string &class_name() const { return class_name_; };
  const std::string &name_template() const { return name_template_; };
  const std::string &help_text() const { return help_text_; };

 private:
  // A function that emits a new Port object of the type class_name.
  std::function<Port *()> port_generator_; 

  // Maps from class names to port builders.  Tracks all port classes (via their
  // PortBuilders).
  static std::map<std::string, PortBuilder> all_port_builders_;

  // Tracks all port instances.
  static std::map<std::string, Port*> all_ports_;

  std::string class_name_;     // The name of this Port class.
  std::string name_template_;  // The port default name prefix.
  std::string help_text_;      // Help text about this port type.

  bool initialized_;  // Has this port class been initialized via InitPortClass()?
};

class Port {
  // overide this section to create a new module -----------------------------
 public:
  Port() = default;
  virtual ~Port() {};

  virtual struct snobj *Init(struct snobj *arg) { return nullptr; }
  virtual void Deinit() {}

  // For one-time initialization of the port's "driver" (optional).
  virtual void InitDriver() {}

  virtual void CollectStats(bool reset) {}

  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt) { return 0; }

  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt) { return 0; }

  // For custom incoming / outgoing queue sizes (optional).
  virtual size_t DefaultIncQueueSize() const { return kDefaultIncQueueSize; }
  virtual size_t DefaultOutQueueSize() const { return kDefaultOutQueueSize; }

  virtual size_t GetFlags() const { return kFlags; }

  // -------------------------------------------------------------------------

 public:
  friend class PortBuilder;

  // Fills in pointed-to structure with this port's stats.
  void GetPortStats(port_stats_t *stats);

  /* queues == NULL if _all_ queues are being acquired/released */
  int AcquireQueues(const struct module *m, packet_dir_t dir,
                    const queue_t *queues, int num);

  void ReleaseQueues(const struct module *m, packet_dir_t dir,
                     const queue_t *queues, int num);

  const std::string &name() const { return name_; };

  const PortBuilder *port_builder() const { return port_builder_; }

  struct snobj *RunCommand(const std::string &user_cmd, struct snobj *arg);

 private:
  // Private methods, for use by PortBuilder.
  void set_name(const std::string &name) { name_ = name; }
  void set_port_builder(const PortBuilder *port_builder) {
    port_builder_ = port_builder;
  }

  std::string name_; // The name of this port instance.

  // Class-wide spec of this type of port.  Non-owning.
  const PortBuilder *port_builder_;

  static const size_t kDefaultIncQueueSize = 256;
  static const size_t kDefaultOutQueueSize = 256;

  static const uint32_t kFlags = 0;

  DISALLOW_COPY_AND_ASSIGN(Port);

  // FIXME: porting in progress ----------------------------
 public:
  queue_t num_queues[PACKET_DIRS];
  size_t queue_size[PACKET_DIRS];

  char mac_addr[ETH_ALEN];

  /* which modules are using this port?
   * TODO: more robust gate keeping */
  const struct module *users[PACKET_DIRS][MAX_QUEUES_PER_DIR];

  struct packet_stats queue_stats[PACKET_DIRS][MAX_QUEUES_PER_DIR];

  /* for stats that do NOT belong to any queues */
  port_stats_t port_stats;
};

// TODO(barath): Change the structure of this, because we can't rely on static
//               initialization order.
#define ADD_DRIVER(_DRIVER, _NAME_TEMPLATE, _HELP) \
  bool __driver__##_DRIVER = PortBuilder::RegisterPortClass(std::function<Port *()>([]() { return new _DRIVER (); }), #_DRIVER, _NAME_TEMPLATE, _HELP);

#endif
