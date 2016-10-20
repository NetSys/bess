#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>

#include <initializer_list>
#include <memory>
#include <sstream>

#include <rte_config.h>

#include <glog/logging.h>

#include "mem_alloc.h"
#include "port.h"

std::map<std::string, Port*> PortBuilder::all_ports_;

Port *PortBuilder::CreatePort(const std::string &name) const {
  Port *p = port_generator_();
  p->set_name(name);
  p->set_port_builder(this);
  return p;
}

bool PortBuilder::AddPort(Port *p) {
  return all_ports_.insert({p->name(), p}).second;
}

int PortBuilder::DestroyPort(Port *p) {
  for (packet_dir_t dir : {PACKET_DIR_INC, PACKET_DIR_OUT}) {
    for (queue_t i = 0; i < p->num_queues[dir]; i++) {
      if (p->users[dir][i]) return -EBUSY;
    }
  }

  all_ports_.erase(p->name());
  p->Deinit();
  delete p;

  return 0;
}

std::string PortBuilder::GenerateDefaultPortName(const std::string &driver_name,
                                                 const std::string &default_template) {
  std::string name_template;

  if (default_template == "") {
    std::ostringstream ss;
    char last_char = '\0';
    for (auto t : default_template) {
      if (last_char != '\0' && islower(last_char) && isupper(t)) ss << '_';

      ss << tolower(t);
      last_char = t;
    }
    name_template = ss.str();
  } else {
    name_template = default_template;
  }

  for (int i = 0;; i++) {
    std::ostringstream ss;
    ss << name_template << i;
    std::string name = ss.str();

    if (!all_ports_.count(name)) return name;  // found an unallocated name!
  }

  promise_unreachable();
}

bool PortBuilder::InitPortClass() {
  if (initialized_) return false;

  std::unique_ptr<Port> p(port_generator_());
  p->InitDriver();
  initialized_ = true;
  return true;
}

bool PortBuilder::RegisterPortClass(std::function<Port *()> port_generator,
                                    const std::string &class_name,
                                    const std::string &name_template,
                                    const std::string &help_text) {
  all_port_builders_holder().emplace(
      std::piecewise_construct,
      std::forward_as_tuple(class_name),
      std::forward_as_tuple(port_generator, class_name, name_template, help_text));
  return true;
}

const std::map<std::string, PortBuilder> &PortBuilder::all_port_builders() {
  return all_port_builders_holder();
}

std::map<std::string, PortBuilder> &PortBuilder::all_port_builders_holder(bool reset) {
  // Maps from class names to port builders.  Tracks all port classes (via their
  // PortBuilders).
  static std::map<std::string, PortBuilder> all_port_builders;

  if (reset) {
    all_port_builders.clear();
  }

  return all_port_builders;
}

const std::map<std::string, Port*> &PortBuilder::all_ports() {
  return all_ports_;
}

void Port::GetPortStats(port_stats_t *stats) {
  CollectStats(false);

  memcpy(stats, &port_stats, sizeof(port_stats_t));

  for (packet_dir_t dir : {PACKET_DIR_INC, PACKET_DIR_OUT}) {
    for (queue_t qid = 0; qid < num_queues[dir]; qid++) {
      const struct packet_stats *qs = &queue_stats[dir][qid];

      (*stats)[dir].packets += qs->packets;
      (*stats)[dir].dropped += qs->dropped;
      (*stats)[dir].bytes += qs->bytes;
    }
  }
}

int Port::AcquireQueues(const struct module *m, packet_dir_t dir,
                        const queue_t *queues, int num) {
  queue_t qid;
  int i;

  if (dir != PACKET_DIR_INC && dir != PACKET_DIR_OUT) {
    LOG(ERROR) << "Incorrect packet dir " << dir;
    return -EINVAL;
  }

  if (queues == NULL) {
    for (qid = 0; qid < num_queues[dir]; qid++) {
      const struct module *user;

      user = users[dir][qid];

      /* the queue is already being used by someone else? */
      if (user && user != m) return -EBUSY;
    }

    for (qid = 0; qid < num_queues[dir]; qid++) users[dir][qid] = m;

    return 0;
  }

  for (i = 0; i < num; i++) {
    const struct module *user;

    qid = queues[i];

    if (qid >= num_queues[dir]) return -EINVAL;

    user = users[dir][qid];

    /* the queue is already being used by someone else? */
    if (user && user != m) return -EBUSY;
  }

  for (i = 0; i < num; i++) {
    qid = queues[i];
    users[dir][qid] = m;
  }

  return 0;
}

void Port::ReleaseQueues(const struct module *m, packet_dir_t dir,
                         const queue_t *queues, int num) {
  queue_t qid;
  int i;

  if (dir != PACKET_DIR_INC && dir != PACKET_DIR_OUT) {
    LOG(ERROR) << "Incorrect packet dir " << dir;
    return;
  }

  if (queues == NULL) {
    for (qid = 0; qid < num_queues[dir]; qid++) {
      if (users[dir][qid] == m) users[dir][qid] = NULL;
    }

    return;
  }

  for (i = 0; i < num; i++) {
    qid = queues[i];
    if (qid >= num_queues[dir]) continue;

    if (users[dir][qid] == m) users[dir][qid] = NULL;
  }
}

/* XXX: Do we need this? Currently not being used anywhere */
// void get_queue_stats(Port *p, packet_dir_t dir, queue_t qid,
//                      struct packet_stats *stats) {
//   memcpy(stats, &p->queue_stats[dir][qid], sizeof(*stats));
// }


