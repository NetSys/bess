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

#include "port.h"

#include <glog/logging.h>

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>

#include "mem_alloc.h"
#include "message.h"

std::map<std::string, Port *> PortBuilder::all_ports_;

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
      if (p->users[dir][i]) {
        return -EBUSY;
      }
    }
  }

  all_ports_.erase(p->name());
  p->DeInit();
  delete p;

  return 0;
}

std::string PortBuilder::GenerateDefaultPortName(
    const std::string &driver_name, const std::string &default_template) {
  std::string name_template;

  if (default_template == "") {
    std::ostringstream ss;
    char last_char = '\0';
    for (auto t : driver_name) {
      if (last_char != '\0' && islower(last_char) && isupper(t))
        ss << '_';

      ss << char(tolower(t));
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

    if (!all_ports_.count(name)) {
      return name;  // found an unallocated name!
    }
  }

  promise_unreachable();
}

bool PortBuilder::InitPortClass() {
  if (initialized_) {
    return false;
  }

  std::unique_ptr<Port> p(port_generator_());
  p->InitDriver();
  initialized_ = true;
  return true;
}

void PortBuilder::InitDrivers() {
  for (auto &pair : all_port_builders()) {
    if (!const_cast<PortBuilder &>(pair.second).InitPortClass()) {
      LOG(WARNING) << "Initializing driver (port class) "
                   << pair.second.class_name() << " failed.";
    }
  }
}

bool PortBuilder::RegisterPortClass(
    std::function<Port *()> port_generator, const std::string &class_name,
    const std::string &name_template, const std::string &help_text,
    std::function<CommandResponse(Port *, const google::protobuf::Any &)>
        init_func) {
  all_port_builders_holder().emplace(
      std::piecewise_construct, std::forward_as_tuple(class_name),
      std::forward_as_tuple(port_generator, class_name, name_template,
                            help_text, init_func));
  return true;
}

const std::map<std::string, PortBuilder> &PortBuilder::all_port_builders() {
  return all_port_builders_holder();
}

std::map<std::string, PortBuilder> &PortBuilder::all_port_builders_holder(
    bool reset) {
  // Maps from class names to port builders.  Tracks all port classes (via their
  // PortBuilders).
  static std::map<std::string, PortBuilder> all_port_builders;

  if (reset) {
    all_port_builders.clear();
  }

  return all_port_builders;
}

const std::map<std::string, Port *> &PortBuilder::all_ports() {
  return all_ports_;
}

void Port::CollectStats(bool) {}

CommandResponse Port::InitWithGenericArg(const google::protobuf::Any &arg) {
  return port_builder_->RunInit(this, arg);
}

Port::PortStats Port::GetPortStats() {
  CollectStats(false);

  PortStats ret = port_stats_;

  for (queue_t qid = 0; qid < num_queues[PACKET_DIR_INC]; qid++) {
    const QueueStats &inc = queue_stats[PACKET_DIR_INC][qid];

    ret.inc.packets += inc.packets;
    ret.inc.dropped += inc.dropped;
    ret.inc.bytes += inc.bytes;
  }

  for (queue_t qid = 0; qid < num_queues[PACKET_DIR_OUT]; qid++) {
    const QueueStats &out = queue_stats[PACKET_DIR_OUT][qid];
    ret.out.packets += out.packets;
    ret.out.dropped += out.dropped;
    ret.out.bytes += out.bytes;
  }

  return ret;
}

int Port::AcquireQueues(const struct module *m, packet_dir_t dir,
                        const queue_t *queues, int num) {
  queue_t qid;
  int i;

  if (dir != PACKET_DIR_INC && dir != PACKET_DIR_OUT) {
    LOG(ERROR) << "Incorrect packet dir " << dir;
    return -EINVAL;
  }

  if (queues == nullptr) {
    for (qid = 0; qid < num_queues[dir]; qid++) {
      const struct module *user;

      user = users[dir][qid];

      /* the queue is already being used by someone else? */
      if (user && user != m) {
        return -EBUSY;
      }
    }

    for (qid = 0; qid < num_queues[dir]; qid++) {
      users[dir][qid] = m;
    }

    return 0;
  }

  for (i = 0; i < num; i++) {
    const struct module *user;

    qid = queues[i];

    if (qid >= num_queues[dir]) {
      return -EINVAL;
    }

    user = users[dir][qid];

    /* the queue is already being used by someone else? */
    if (user && user != m) {
      return -EBUSY;
    }
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

  if (queues == nullptr) {
    for (qid = 0; qid < num_queues[dir]; qid++) {
      if (users[dir][qid] == m)
        users[dir][qid] = nullptr;
    }

    return;
  }

  for (i = 0; i < num; i++) {
    qid = queues[i];
    if (qid >= num_queues[dir])
      continue;

    if (users[dir][qid] == m)
      users[dir][qid] = nullptr;
  }
}
