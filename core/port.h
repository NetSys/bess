#ifndef _PORT_H_
#define _PORT_H_

#include <stdint.h>

#include "common.h"
#include "log.h"
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

#if 0
struct module;

Port {
	char *name;

	const Driver *driver;

	/* which modules are using this port?
	 * TODO: more robust gate keeping */
	const struct module *users[PACKET_DIRS][MAX_QUEUES_PER_DIR];

	char mac_addr[ETH_ALEN];

	queue_t num_queues[PACKET_DIRS];
	int queue_size[PACKET_DIRS];

	struct packet_stats queue_stats[PACKET_DIRS][MAX_QUEUES_PER_DIR];

	/* for stats that do NOT belong to any queues */
	port_stats_t port_stats;

	void *priv[0];
};

static inline void *get_port_priv(Port *p)
{
	return (void *)(p + 1);
}
#endif

class Port;

class Driver {
 public:
  Driver(const std::string &driver_name, const std::string &name_template,
         const std::string &help)
      : name_(driver_name), name_template_(name_template), help_(help) {
    int ret = ns_insert(NS_TYPE_DRIVER, driver_name.c_str(),
                        static_cast<void *>(this));
    if (ret < 0) {
      log_err("ns_insert() failure for driver '%s'\n", driver_name.c_str());
    }
  }

  static void InitDriver(){};

  virtual Port *CreatePort(const std::string &name) const = 0;
  virtual void Init() {}

  std::string Name() const { return name_; }
  std::string NameTemplate() const { return name_template_; }
  std::string Help() const { return help_; }

  virtual size_t DefaultQueueSize(packet_dir_t dir) const = 0;
  virtual uint32_t GetFlags() const = 0;

 private:
  std::string name_;
  std::string name_template_;
  std::string help_;

  size_t def_size_inc_q;
  size_t def_size_out_q;
};

template <typename T>
class DriverRegister : public Driver {
 public:
  DriverRegister(const std::string &class_name,
                 const std::string &name_template, const std::string &help)
      : Driver(class_name, name_template, help){};

  virtual void InitDriver() { T::InitDriver(); }

  virtual Port *CreatePort(const std::string &name) const {
    T *m = new T;
    m->name_ = name;
    m->driver_ = this;
    return m;
  }

  virtual size_t DefaultQueueSize(packet_dir_t dir) const {
    if (dir == PACKET_DIR_INC)
      return T::kDefaultIncQueueSize;
    else
      return T::kDefaultOutQueueSize;
  }

  virtual uint32_t GetFlags() const { return T::kFlags; }
};

class Port {
  // overide this section to create a new module -----------------------------
 public:
  Port() = default;
  virtual ~Port() = 0;

  virtual struct snobj *Init(struct snobj *arg) { return nullptr; }
  virtual void Deinit() {}

  virtual void CollectStats(bool reset) {}

  virtual int RecvPackets(queue_t qid, snb_array_t pkts, int cnt) { return 0; }

  virtual int SendPackets(queue_t qid, snb_array_t pkts, int cnt) { return 0; }

  static const size_t kDefaultIncQueueSize = 256;
  static const size_t kDefaultOutQueueSize = 256;

  static const uint32_t kFlags = 0;

  // -------------------------------------------------------------------------

 public:
  const Driver *GetDriver() const { return driver_; };
  std::string Name() const { return name_; };

  struct snobj *RunCommand(const std::string &user_cmd, struct snobj *arg);

 private:
  // non-copyable and non-movable by default
  Port(Port &) = delete;
  Port &operator=(Port &) = delete;
  Port(Port &&) = delete;
  Port &operator=(Port &&) = delete;

  std::string name_;
  const Driver *driver_;

  template <typename T>
  friend class DriverRegister;

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

size_t list_drivers(const Driver **p_arr, size_t arr_size, size_t offset);

const Driver *find_driver(const char *name);

int add_driver(const Driver *driver);

void init_drivers();

size_t list_ports(const Port **p_arr, size_t arr_size, size_t offset);
Port *find_port(const char *name);

Port *create_port(const char *name, const Driver *driver, struct snobj *arg,
                  struct snobj **perr);

int destroy_port(Port *p);

void get_port_stats(Port *p, port_stats_t *stats);

void get_queue_stats(Port *p, packet_dir_t dir, queue_t qid,
                     struct packet_stats *stats);

/* quques == NULL if _all_ queues are being acquired/released */
int acquire_queues(Port *p, const struct module *m, packet_dir_t dir,
                   const queue_t *queues, int num_queues);
void release_queues(Port *p, const struct module *m, packet_dir_t dir,
                    const queue_t *queues, int num_queues);

#define ADD_DRIVER(_DRIVER, _NAME_TEMPLATE, _HELP) \
  DriverRegister<_DRIVER> __driver__##_DRIVER(#_DRIVER, _NAME_TEMPLATE, _HELP);

#endif
