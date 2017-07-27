// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// Copyright (c) 2017, Cloudigo.
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

#include "bessctl.h"

#include <thread>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "pb/service.grpc.pb.h"
#pragma GCC diagnostic pop

#include "bessd.h"
#include "gate.h"
#include "hooks/tcpdump.h"
#include "hooks/track.h"
#include "message.h"
#include "metadata.h"
#include "module.h"
#include "opts.h"
#include "packet.h"
#include "port.h"
#include "scheduler.h"
#include "traffic_class.h"
#include "utils/ether.h"
#include "utils/format.h"
#include "utils/time.h"
#include "worker.h"

#include <rte_mempool.h>
#include <rte_ring.h>

using grpc::Status;
using grpc::ServerContext;

using bess::TrafficClassBuilder;
using namespace bess::pb;

template <typename T>
static inline Status return_with_error(T* response, int code, const char* fmt,
                                       ...) {
  va_list ap;
  va_start(ap, fmt);
  response->mutable_error()->set_code(code);
  response->mutable_error()->set_errmsg(bess::utils::FormatVarg(fmt, ap));
  va_end(ap);
  return Status::OK;
}

template <typename T>
static inline Status return_with_errno(T* response, int code) {
  response->mutable_error()->set_code(code);
  response->mutable_error()->set_errmsg(strerror(code));
  return Status::OK;
}

static CommandResponse enable_hook_for_module(
    const Module* m, gate_idx_t gate_idx, bool is_igate, bool use_gate,
    const bess::GateHookFactory& factory, const google::protobuf::Any& arg) {
  int ret;

  if (use_gate) {
    bess::Gate* gate = nullptr;
    if (is_igate) {
      if (!is_active_gate(m->igates(), gate_idx)) {
        return CommandFailure(EINVAL, "Input gate '%hu' does not exist",
                              gate_idx);
      }
      gate = m->igates()[gate_idx];
      bess::GateHook* hook = factory.CreateGateHook();
      CommandResponse init_ret = factory.InitGateHook(hook, gate, arg);
      if (init_ret.error().code() != 0) {
        delete hook;
        return init_ret;
      }
      if ((ret = gate->AddHook(hook))) {
        return CommandFailure(ret, "Failed to track input gate '%hu'",
                              gate_idx);
      }
    } else {
      if (!is_active_gate(m->ogates(), gate_idx)) {
        return CommandFailure(EINVAL, "Output gate '%hu' does not exist",
                              gate_idx);
      }
      gate = m->ogates()[gate_idx];
      bess::GateHook* hook = factory.CreateGateHook();
      CommandResponse init_ret = factory.InitGateHook(hook, gate, arg);
      if (init_ret.error().code() != 0) {
        delete hook;
        return init_ret;
      }
      if ((ret = gate->AddHook(hook))) {
        delete hook;
        return CommandFailure(ret, "Failed to track output gate '%hu'",
                              gate_idx);
      }
    }
    return CommandSuccess();
  }

  if (is_igate) {
    for (auto& gate : m->igates()) {
      if (!gate) {
        continue;
      }
      bess::GateHook* hook = factory.CreateGateHook();
      CommandResponse init_ret = factory.InitGateHook(hook, gate, arg);
      if (init_ret.error().code() != 0) {
        delete hook;
        return init_ret;
      }
      if ((ret = gate->AddHook(hook))) {
        delete hook;
        return CommandFailure(ret, "Failed to track input gate '%hu'",
                              gate->gate_idx());
      }
    }
  } else {
    for (auto& gate : m->ogates()) {
      if (!gate) {
        continue;
      }
      bess::GateHook* hook = factory.CreateGateHook();
      CommandResponse init_ret = factory.InitGateHook(hook, gate, arg);
      if (init_ret.error().code() != 0) {
        delete hook;
        return init_ret;
      }
      if ((ret = gate->AddHook(hook))) {
        delete hook;
        return CommandFailure(ret, "Failed to track output gate '%hu'",
                              gate->gate_idx());
      }
    }
  }
  return CommandSuccess();
}

static CommandResponse disable_hook_for_module(const Module* m,
                                               gate_idx_t gate_idx,
                                               bool is_igate, bool use_gate,
                                               const std::string& hook) {
  if (use_gate) {
    if (!is_igate && !is_active_gate(m->ogates(), gate_idx)) {
      return CommandFailure(EINVAL, "Output gate '%hu' does not exist",
                            gate_idx);
    }

    if (is_igate && !is_active_gate(m->igates(), gate_idx)) {
      return CommandFailure(EINVAL, "Input gate '%hu' does not exist",
                            gate_idx);
    }

    if (is_igate) {
      m->igates()[gate_idx]->RemoveHook(hook);
      return CommandSuccess();
    }

    m->ogates()[gate_idx]->RemoveHook(hook);
    return CommandSuccess();
  }

  if (is_igate) {
    for (auto& gate : m->igates()) {
      if (!gate) {
        continue;
      }
      gate->RemoveHook(hook);
    }
  } else {
    for (auto& gate : m->ogates()) {
      if (!gate) {
        continue;
      }
      gate->RemoveHook(hook);
    }
  }
  return CommandSuccess();
}

static int collect_igates(Module* m, GetModuleInfoResponse* response) {
  for (const auto& g : m->igates()) {
    if (!g) {
      continue;
    }

    GetModuleInfoResponse_IGate* igate = response->add_igates();

    Track* t = reinterpret_cast<Track*>(g->FindHook(Track::kName));

    if (t) {
      igate->set_cnt(t->cnt());
      igate->set_pkts(t->pkts());
      igate->set_bytes(t->bytes());
      igate->set_timestamp(get_epoch_time());
    }

    igate->set_igate(g->gate_idx());
    for (const auto& og : g->ogates_upstream()) {
      GetModuleInfoResponse_IGate_OGate* ogate = igate->add_ogates();
      ogate->set_ogate(og->gate_idx());
      ogate->set_name(og->module()->name());
    }
  }

  return 0;
}

static int collect_ogates(Module* m, GetModuleInfoResponse* response) {
  for (const auto& g : m->ogates()) {
    if (!g) {
      continue;
    }

    GetModuleInfoResponse_OGate* ogate = response->add_ogates();

    ogate->set_ogate(g->gate_idx());
    Track* t = reinterpret_cast<Track*>(g->FindHook(Track::kName));
    if (t) {
      ogate->set_cnt(t->cnt());
      ogate->set_pkts(t->pkts());
      ogate->set_bytes(t->bytes());
      ogate->set_timestamp(get_epoch_time());
    }
    ogate->set_name(g->igate()->module()->name());
    ogate->set_igate(g->igate()->gate_idx());
  }

  return 0;
}

static int collect_metadata(Module* m, GetModuleInfoResponse* response) {
  size_t i = 0;
  for (const auto& it : m->all_attrs()) {
    GetModuleInfoResponse_Attribute* attr = response->add_metadata();

    attr->set_name(it.name);
    attr->set_size(it.size);

    switch (it.mode) {
      case bess::metadata::Attribute::AccessMode::kRead:
        attr->set_mode("read");
        break;
      case bess::metadata::Attribute::AccessMode::kWrite:
        attr->set_mode("write");
        break;
      case bess::metadata::Attribute::AccessMode::kUpdate:
        attr->set_mode("update");
        break;
      default:
        DCHECK(0);
    }

    attr->set_offset(m->attr_offset(i));
    i++;
  }

  return 0;
}

static ::Port* create_port(const std::string& name, const PortBuilder& driver,
                           queue_t num_inc_q, queue_t num_out_q,
                           size_t size_inc_q, size_t size_out_q,
                           const std::string& mac_addr_str,
                           const google::protobuf::Any& arg, pb_error_t* perr) {
  std::unique_ptr<::Port> p;

  if (num_inc_q == 0) {
    num_inc_q = 1;
  }

  if (num_out_q == 0) {
    num_out_q = 1;
  }

  bess::utils::Ethernet::Address mac_addr;

  if (mac_addr_str.length() > 0) {
    if (!mac_addr.FromString(mac_addr_str)) {
      perr->set_code(EINVAL);
      perr->set_errmsg(
          "MAC address should be "
          "formatted as a string "
          "xx:xx:xx:xx:xx:xx");
      return nullptr;
    }
  } else {
    mac_addr.Randomize();
  }

  if (num_inc_q > MAX_QUEUES_PER_DIR || num_out_q > MAX_QUEUES_PER_DIR) {
    perr->set_code(EINVAL);
    perr->set_errmsg("Invalid number of queues");
    return nullptr;
  }

  if (size_inc_q > MAX_QUEUE_SIZE || size_out_q > MAX_QUEUE_SIZE) {
    perr->set_code(EINVAL);
    perr->set_errmsg("Invalid queue size");
    return nullptr;
  }

  std::string port_name;

  if (name.length() > 0) {
    if (PortBuilder::all_ports().count(name)) {
      perr->set_code(EEXIST);
      perr->set_errmsg(
          bess::utils::Format("Port '%s' already exists", name.c_str()));
      return nullptr;
    }
    port_name = name;
  } else {
    port_name = PortBuilder::GenerateDefaultPortName(driver.class_name(),
                                                     driver.name_template());
  }

  // Try to create and initialize the port.
  p.reset(driver.CreatePort(port_name));

  if (size_inc_q == 0) {
    size_inc_q = p->DefaultIncQueueSize();
  }

  if (size_out_q == 0) {
    size_out_q = p->DefaultOutQueueSize();
  }

  bess::utils::Copy(p->mac_addr, mac_addr.bytes, ETH_ALEN);
  p->num_queues[PACKET_DIR_INC] = num_inc_q;
  p->num_queues[PACKET_DIR_OUT] = num_out_q;
  p->queue_size[PACKET_DIR_INC] = size_inc_q;
  p->queue_size[PACKET_DIR_OUT] = size_out_q;

  // DPDK functions may be called, so be prepared
  ctx.SetNonWorker();

  CommandResponse ret = p->InitWithGenericArg(arg);

  {
    google::protobuf::Any empty;

    if (ret.data().SerializeAsString() != empty.SerializeAsString()) {
      LOG(WARNING) << port_name << "::" << driver.class_name()
                   << " Init() returned non-empty response: "
                   << ret.data().DebugString();
    }
  }

  if (ret.error().code() != 0) {
    *perr = ret.error();
    return nullptr;
  }

  if (!PortBuilder::AddPort(p.get())) {
    return nullptr;
  }

  return p.release();
}

static Module* create_module(const std::string& name,
                             const ModuleBuilder& builder,
                             const google::protobuf::Any& arg,
                             pb_error_t* perr) {
  Module* m = builder.CreateModule(name, &bess::metadata::default_pipeline);

  // DPDK functions may be called, so be prepared
  ctx.SetNonWorker();

  CommandResponse ret = m->InitWithGenericArg(arg);

  {
    google::protobuf::Any empty;

    if (ret.data().SerializeAsString() != empty.SerializeAsString()) {
      LOG(WARNING) << name << "::" << builder.class_name()
                   << " Init() returned non-empty response: "
                   << ret.data().DebugString();
    }
  }

  if (ret.error().code() != 0) {
    *perr = ret.error();
    return nullptr;
  }

  if (!ModuleBuilder::AddModule(m)) {
    *perr = pb_errno(ENOMEM);
    return nullptr;
  }
  return m;
}

static void collect_tc(const bess::TrafficClass* c, int wid,
                       ListTcsResponse_TrafficClassStatus* status) {
  if (c->parent()) {
    status->set_parent(c->parent()->name());
  }

  status->mutable_class_()->set_name(c->name());
  status->mutable_class_()->set_blocked(c->blocked());

  if (c->policy() >= 0 && c->policy() < bess::NUM_POLICIES) {
    status->mutable_class_()->set_policy(bess::TrafficPolicyName[c->policy()]);
  } else {
    status->mutable_class_()->set_policy("invalid");
  }

  status->mutable_class_()->set_wid(wid);

  if (c->policy() == bess::POLICY_RATE_LIMIT) {
    const bess::RateLimitTrafficClass* rl =
        reinterpret_cast<const bess::RateLimitTrafficClass*>(c);
    std::string resource = bess::ResourceName.at(rl->resource());
    int64_t limit = rl->limit_arg();
    int64_t max_burst = rl->max_burst_arg();
    status->mutable_class_()->mutable_limit()->insert({resource, limit});
    status->mutable_class_()->mutable_max_burst()->insert(
        {resource, max_burst});
  }
}

class BESSControlImpl final : public BESSControl::Service {
 public:
  void set_shutdown_func(const std::function<void()>& func) {
    shutdown_func_ = func;
  }

  Status GetVersion(ServerContext*, const EmptyRequest*,
                    VersionResponse* response) override {
    response->set_version(google::VersionString());
    return Status::OK;
  }

  Status ResetAll(ServerContext* context, const EmptyRequest* request,
                  EmptyResponse* response) override {
    Status status;

    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    LOG(INFO) << "*** ResetAll requested ***";

    status = ResetModules(context, request, response);
    if (response->error().code() != 0) {
      return status;
    }

    status = ResetPorts(context, request, response);
    if (response->error().code() != 0) {
      return status;
    }

    status = ResetTcs(context, request, response);
    if (response->error().code() != 0) {
      return status;
    }

    status = ResetWorkers(context, request, response);
    if (response->error().code() != 0) {
      return status;
    }

    return Status::OK;
  }

  Status PauseAll(ServerContext*, const EmptyRequest*,
                  EmptyResponse*) override {
    pause_all_workers();
    LOG(INFO) << "*** All workers have been paused ***";
    return Status::OK;
  }

  Status PauseWorker(ServerContext*, const PauseWorkerRequest* req,
                     EmptyResponse*) override {
    int wid = req->wid();
    // TODO: It should be made harder to wreak havoc on the rest of the daemon
    // when using PauseWorker(). For now a warning and suggestion that this is
    // for experts only is sufficient.
    LOG(WARNING) << "PauseWorker() is an experimental operation and should be"
                 << " used with care. Long-term support not guaranteed.";
    pause_worker(wid);
    LOG(INFO) << "*** Worker " << wid << " has been paused ***";
    return Status::OK;
  }

  Status ResumeAll(ServerContext*, const EmptyRequest*,
                   EmptyResponse*) override {
    LOG(INFO) << "*** Resuming ***";
    if (!is_any_worker_running()) {
      attach_orphans();
    }
    resume_all_workers();
    return Status::OK;
  }

  Status ResumeWorker(ServerContext*, const ResumeWorkerRequest* req,
                      EmptyResponse*) override {
    int wid = req->wid();
    LOG(INFO) << "*** Resuming worker " << wid << " ***";
    resume_worker(wid);
    return Status::OK;
  }

  Status ResetWorkers(ServerContext*, const EmptyRequest*,
                      EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    destroy_all_workers();
    LOG(INFO) << "*** All workers have been destroyed ***";
    return Status::OK;
  }

  Status ListWorkers(ServerContext*, const EmptyRequest*,
                     ListWorkersResponse* response) override {
    for (int wid = 0; wid < Worker::kMaxWorkers; wid++) {
      if (!is_worker_active(wid))
        continue;
      ListWorkersResponse_WorkerStatus* status = response->add_workers_status();
      status->set_wid(wid);
      status->set_running(is_worker_running(wid));
      status->set_core(workers[wid]->core());
      status->set_num_tcs(workers[wid]->scheduler()->NumTcs());
      status->set_silent_drops(workers[wid]->silent_drops());
    }
    return Status::OK;
  }

  Status AddWorker(ServerContext*, const AddWorkerRequest* request,
                   EmptyResponse* response) override {
    uint64_t wid = request->wid();
    if (wid >= Worker::kMaxWorkers) {
      return return_with_error(response, EINVAL, "Invalid worker id");
    }
    uint64_t core = request->core();
    if (!is_cpu_present(core)) {
      return return_with_error(response, EINVAL, "Invalid core %d", core);
    }
    if (is_worker_active(wid)) {
      return return_with_error(response, EEXIST, "worker:%d is already active",
                               wid);
    }
    const std::string& scheduler = request->scheduler();
    if (scheduler != "" && scheduler != "experimental") {
      return return_with_error(response, EINVAL, "Invalid scheduler %s",
                               scheduler.c_str());
    }

    launch_worker(wid, core, scheduler);
    return Status::OK;
  }

  Status DestroyWorker(ServerContext*, const DestroyWorkerRequest* request,
                       EmptyResponse* response) override {
    uint64_t wid = request->wid();
    if (wid >= Worker::kMaxWorkers) {
      return return_with_error(response, EINVAL, "Invalid worker id");
    }
    Worker* worker = workers[wid];
    if (!worker) {
      return return_with_error(response, ENOENT, "Worker %d is not active",
                               wid);
    }

    bess::TrafficClass* root = workers[wid]->scheduler()->root();
    if (root) {
      for (const auto& it : bess::TrafficClassBuilder::all_tcs()) {
        bess::TrafficClass* c = it.second;
        if (c->policy() == bess::POLICY_LEAF && c->Root() == root) {
          return return_with_error(response, EBUSY,
                                   "Worker %d has active tasks ", wid);
        }
      }
    }

    destroy_worker(wid);
    return Status::OK;
  }

  Status ResetTcs(ServerContext*, const EmptyRequest*,
                  EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    if (!TrafficClassBuilder::ClearAll()) {
      return return_with_error(response, EBUSY, "TCs still have tasks");
    }

    return Status::OK;
  }

  Status ListTcs(ServerContext*, const ListTcsRequest* request,
                 ListTcsResponse* response) override {
    int wid_filter = request->wid();
    if (wid_filter >= Worker::kMaxWorkers) {
      return return_with_error(response, EINVAL,
                               "'wid' must be between 0 and %d",
                               Worker::kMaxWorkers - 1);
    } else if (wid_filter < 0) {
      wid_filter = Worker::kAnyWorker;
    }

    for (const auto& tc_pair : bess::TrafficClassBuilder::all_tcs()) {
      bess::TrafficClass* c = tc_pair.second;
      int wid = c->WorkerId();
      if (wid_filter == Worker::kAnyWorker || wid_filter == wid) {
        // WRR and Priority TCs associate share/priority to each child
        if (c->policy() == bess::POLICY_WEIGHTED_FAIR) {
          const auto* wrr_parent =
              static_cast<const bess::WeightedFairTrafficClass*>(c);
          for (const auto& child_data : wrr_parent->children()) {
            auto* status = response->add_classes_status();
            collect_tc(child_data.first, wid, status);
            status->mutable_class_()->set_share(child_data.second);
          }
        } else if (c->policy() == bess::POLICY_PRIORITY) {
          const auto* prio_parent =
              static_cast<const bess::PriorityTrafficClass*>(c);
          for (const auto& child_data : prio_parent->children()) {
            auto* status = response->add_classes_status();
            collect_tc(child_data.c_, wid, status);
            status->mutable_class_()->set_priority(child_data.priority_);
          }
        } else {
          for (const auto* child : c->Children()) {
            auto* status = response->add_classes_status();
            collect_tc(child, wid, status);
          }
        }

        if (!c->parent()) {
          auto* status = response->add_classes_status();
          collect_tc(c, wid, status);
        }
      }
    }

    return Status::OK;
  }

  Status CheckSchedulingConstraints(
      ServerContext*, const EmptyRequest*,
      CheckSchedulingConstraintsResponse* response) override {
    // Start by attaching orphans -- this is essential to make sure we visit
    // every TC.
    if (!is_any_worker_running()) {
      // If any worker is running (i.e., not everything is paused), then there
      // is no point in attaching orphans.
      attach_orphans();
    }
    propagate_active_worker();
    LOG(INFO) << "Checking scheduling constraints";
    // Check constraints around chains run by each worker. This checks that
    // global constraints are met.
    for (int i = 0; i < Worker::kMaxWorkers; i++) {
      if (workers[i] == nullptr) {
        continue;
      }
      int socket = 1ull << workers[i]->socket();
      int core = workers[i]->core();
      bess::TrafficClass* root = workers[i]->scheduler()->root();

      for (const auto& tc_pair : bess::TrafficClassBuilder::all_tcs()) {
        bess::TrafficClass* c = tc_pair.second;
        if (c->policy() == bess::POLICY_LEAF && root == c->Root()) {
          auto leaf = static_cast<bess::LeafTrafficClass<Task>*>(c);
          int constraints = leaf->Task().GetSocketConstraints();
          if ((constraints & socket) == 0) {
            LOG(WARNING) << "Scheduler constraints are violated for wid " << i
                         << " socket " << socket << " constraint "
                         << constraints;
            auto violation = response->add_violations();
            violation->set_name(c->name());
            violation->set_constraint(constraints);
            violation->set_assigned_node(workers[i]->socket());
            violation->set_assigned_core(core);
          }
        }
      }
    }

    // Check local constraints
    for (const auto& pair : ModuleBuilder::all_modules()) {
      const Module* m = pair.second;
      auto ret = m->CheckModuleConstraints();
      if (ret != CHECK_OK) {
        LOG(WARNING) << "Module " << m->name() << " failed check " << ret;
        auto module = response->add_modules();
        module->set_name(m->name());
        if (ret == CHECK_FATAL_ERROR) {
          LOG(WARNING) << " --- FATAL CONSTRAINT FAILURE ---";
          response->set_fatal(true);
        }
      }
    }
    return Status::OK;
  }

  Status AddTc(ServerContext*, const AddTcRequest* request,
               EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    const char* tc_name = request->class_().name().c_str();
    if (request->class_().name().length() == 0) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    } else if (tc_name[0] == '!') {
      return return_with_error(response, EINVAL,
                               "TC names starting with \'!\' are reserved");
    }

    if (TrafficClassBuilder::all_tcs().count(tc_name)) {
      return return_with_error(response, EINVAL, "Name '%s' already exists",
                               tc_name);
    }

    const std::string& policy = request->class_().policy();

    bess::TrafficClass* c = nullptr;
    if (policy == bess::TrafficPolicyName[bess::POLICY_PRIORITY]) {
      c = reinterpret_cast<bess::TrafficClass*>(
          TrafficClassBuilder::CreateTrafficClass<bess::PriorityTrafficClass>(
              tc_name));
    } else if (policy == bess::TrafficPolicyName[bess::POLICY_WEIGHTED_FAIR]) {
      const std::string& resource = request->class_().resource();
      if (bess::ResourceMap.count(resource) == 0) {
        return return_with_error(response, EINVAL, "Invalid resource");
      }
      c = reinterpret_cast<bess::TrafficClass*>(
          TrafficClassBuilder::CreateTrafficClass<
              bess::WeightedFairTrafficClass>(tc_name,
                                              bess::ResourceMap.at(resource)));
    } else if (policy == bess::TrafficPolicyName[bess::POLICY_ROUND_ROBIN]) {
      c = reinterpret_cast<bess::TrafficClass*>(
          TrafficClassBuilder::CreateTrafficClass<bess::RoundRobinTrafficClass>(
              tc_name));
    } else if (policy == bess::TrafficPolicyName[bess::POLICY_RATE_LIMIT]) {
      uint64_t limit = 0;
      uint64_t max_burst = 0;
      const std::string& resource = request->class_().resource();
      const auto& limits = request->class_().limit();
      const auto& max_bursts = request->class_().max_burst();
      if (bess::ResourceMap.count(resource) == 0) {
        return return_with_error(response, EINVAL, "Invalid resource");
      }
      if (limits.find(resource) != limits.end()) {
        limit = limits.at(resource);
      }
      if (max_bursts.find(resource) != max_bursts.end()) {
        max_burst = max_bursts.at(resource);
      }
      c = reinterpret_cast<bess::TrafficClass*>(
          TrafficClassBuilder::CreateTrafficClass<bess::RateLimitTrafficClass>(
              tc_name, bess::ResourceMap.at(resource), limit, max_burst));
    } else if (policy == bess::TrafficPolicyName[bess::POLICY_LEAF]) {
      return return_with_error(response, EINVAL,
                               "Cannot create leaf TC. Use "
                               "UpdateTcParentRequest message");
    } else {
      return return_with_error(response, EINVAL, "Invalid traffic policy");
    }

    if (!c) {
      return return_with_error(response, ENOMEM, "CreateTrafficClass failed");
    }

    return AttachTc(c, request->class_(), response);
  }

  Status UpdateTcParams(ServerContext*, const UpdateTcParamsRequest* request,
                        EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    bess::TrafficClass* c = FindTc(request->class_(), response);
    if (!c) {
      return Status::OK;
    }

    if (c->policy() == bess::POLICY_RATE_LIMIT) {
      bess::RateLimitTrafficClass* tc =
          reinterpret_cast<bess::RateLimitTrafficClass*>(c);
      const std::string& resource = request->class_().resource();
      const auto& limits = request->class_().limit();
      const auto& max_bursts = request->class_().max_burst();
      if (bess::ResourceMap.count(resource) == 0) {
        return return_with_error(response, EINVAL, "Invalid resource");
      }
      tc->set_resource(bess::ResourceMap.at(resource));
      if (limits.find(resource) != limits.end()) {
        tc->set_limit(limits.at(resource));
      }
      if (max_bursts.find(resource) != max_bursts.end()) {
        tc->set_max_burst(max_bursts.at(resource));
      }
    } else if (c->policy() == bess::POLICY_WEIGHTED_FAIR) {
      bess::WeightedFairTrafficClass* tc =
          reinterpret_cast<bess::WeightedFairTrafficClass*>(c);
      const std::string& resource = request->class_().resource();
      if (bess::ResourceMap.count(resource) == 0) {
        return return_with_error(response, EINVAL, "Invalid resource");
      }
      tc->set_resource(bess::ResourceMap.at(resource));
    } else {
      return return_with_error(response, EINVAL,
                               "Only 'rate_limit' and"
                               " 'weighted_fair' can be updated");
    }

    return Status::OK;
  }

  Status UpdateTcParent(ServerContext*, const UpdateTcParentRequest* request,
                        EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    bess::TrafficClass* c = FindTc(request->class_(), response);
    if (!c) {
      return Status::OK;
    }

    if (c->policy() == bess::POLICY_LEAF) {
      if (!detach_tc(c)) {
        return return_with_error(response, EINVAL,
                                 "Cannot detach '%s'"
                                 " from parent",
                                 request->class_().name().c_str());
      }
    }

    // XXX Leaf nodes can always be moved, other nodes can be moved only if
    // they're orphans. The scheduler maintains state which would need to be
    // updated otherwise.
    if (c->policy() != bess::POLICY_LEAF) {
      if (!remove_tc_from_orphan(c)) {
        return return_with_error(response, EINVAL,
                                 "Cannot detach '%s'."
                                 " while it is part of a worker",
                                 request->class_().name().c_str());
      }
    }

    return AttachTc(c, request->class_(), response);
  }

  Status GetTcStats(ServerContext*, const GetTcStatsRequest* request,
                    GetTcStatsResponse* response) override {
    const char* tc_name = request->name().c_str();

    bess::TrafficClass* c;

    if (request->name().length() == 0) {
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    }

    const auto& tcs = TrafficClassBuilder::all_tcs();
    const auto& it = tcs.find(tc_name);
    if (it == tcs.end()) {
      return return_with_error(response, ENOENT, "No TC '%s' found", tc_name);
    }
    c = it->second;

    response->set_timestamp(get_epoch_time());
    response->set_count(c->stats().usage[bess::RESOURCE_COUNT]);
    response->set_cycles(c->stats().usage[bess::RESOURCE_CYCLE]);
    response->set_packets(c->stats().usage[bess::RESOURCE_PACKET]);
    response->set_bits(c->stats().usage[bess::RESOURCE_BIT]);

    return Status::OK;
  }

  Status ListDrivers(ServerContext*, const EmptyRequest*,
                     ListDriversResponse* response) override {
    for (const auto& pair : PortBuilder::all_port_builders()) {
      const PortBuilder& builder = pair.second;
      response->add_driver_names(builder.class_name());
    }

    return Status::OK;
  }

  Status GetDriverInfo(ServerContext*, const GetDriverInfoRequest* request,
                       GetDriverInfoResponse* response) override {
    if (request->driver_name().length() == 0) {
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    }

    const auto& it =
        PortBuilder::all_port_builders().find(request->driver_name());
    if (it == PortBuilder::all_port_builders().end()) {
      return return_with_error(response, ENOENT, "No driver '%s' found",
                               request->driver_name().c_str());
    }

#if 0
                        for (int i = 0; i < MAX_COMMANDS; i++) {
                          if (!drv->commands[i].cmd)
                            break;
                          response->add_commands(drv->commands[i].cmd);
                        }
#endif
    response->set_name(it->second.class_name());
    response->set_help(it->second.help_text());

    return Status::OK;
  }

  Status ResetPorts(ServerContext*, const EmptyRequest*,
                    EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    for (auto it = PortBuilder::all_ports().cbegin();
         it != PortBuilder::all_ports().end();) {
      auto it_next = std::next(it);
      ::Port* p = it->second;

      int ret = PortBuilder::DestroyPort(p);
      if (ret)
        return return_with_errno(response, -ret);

      it = it_next;
    }

    LOG(INFO) << "*** All ports have been destroyed ***";
    return Status::OK;
  }

  Status ListPorts(ServerContext*, const EmptyRequest*,
                   ListPortsResponse* response) override {
    for (const auto& pair : PortBuilder::all_ports()) {
      const ::Port* p = pair.second;
      bess::pb::ListPortsResponse::Port* port = response->add_ports();

      port->set_name(p->name());
      port->set_driver(p->port_builder()->class_name());

      bess::utils::Ethernet::Address mac_addr;
      bess::utils::Copy(mac_addr.bytes, p->mac_addr, ETH_ALEN);
      port->set_mac_addr(mac_addr.ToString());
    }

    return Status::OK;
  }

  Status CreatePort(ServerContext*, const CreatePortRequest* request,
                    CreatePortResponse* response) override {
    const char* driver_name;
    ::Port* port = nullptr;

    VLOG(1) << "CreatePortRequest from client:" << std::endl
            << request->DebugString();

    if (request->driver().length() == 0)
      return return_with_error(response, EINVAL, "Missing 'driver' field");

    driver_name = request->driver().c_str();
    const auto& builders = PortBuilder::all_port_builders();
    const auto& it = builders.find(driver_name);
    if (it == builders.end()) {
      return return_with_error(response, ENOENT, "No port driver '%s' found",
                               driver_name);
    }

    const PortBuilder& builder = it->second;
    pb_error_t* error = response->mutable_error();

    port = create_port(request->name(), builder, request->num_inc_q(),
                       request->num_out_q(), request->size_inc_q(),
                       request->size_out_q(), request->mac_addr(),
                       request->arg(), error);

    if (!port)
      return Status::OK;

    response->set_name(port->name());

    bess::utils::Ethernet::Address mac_addr;
    bess::utils::Copy(mac_addr.bytes, port->mac_addr, ETH_ALEN);
    response->set_mac_addr(mac_addr.ToString());

    return Status::OK;
  }

  Status DestroyPort(ServerContext*, const DestroyPortRequest* request,
                     EmptyResponse* response) override {
    const char* port_name;
    int ret;

    if (!request->name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");

    port_name = request->name().c_str();
    const auto& it = PortBuilder::all_ports().find(port_name);
    if (it == PortBuilder::all_ports().end())
      return return_with_error(response, ENOENT, "No port `%s' found",
                               port_name);

    ret = PortBuilder::DestroyPort(it->second);
    if (ret) {
      return return_with_errno(response, -ret);
    }

    return Status::OK;
  }

  Status GetPortStats(ServerContext*, const GetPortStatsRequest* request,
                      GetPortStatsResponse* response) override {
    const auto& it = PortBuilder::all_ports().find(request->name());
    if (it == PortBuilder::all_ports().end()) {
      return return_with_error(response, ENOENT, "No port '%s' found",
                               request->name().c_str());
    }

    ::Port::PortStats stats = it->second->GetPortStats();

    response->mutable_inc()->set_packets(stats.inc.packets);
    response->mutable_inc()->set_dropped(stats.inc.dropped);
    response->mutable_inc()->set_bytes(stats.inc.bytes);

    response->mutable_out()->set_packets(stats.out.packets);
    response->mutable_out()->set_dropped(stats.out.dropped);
    response->mutable_out()->set_bytes(stats.out.bytes);

    response->set_timestamp(get_epoch_time());

    return Status::OK;
  }

  Status GetLinkStatus(ServerContext*, const GetLinkStatusRequest* request,
                       GetLinkStatusResponse* response) override {
    const auto& it = PortBuilder::all_ports().find(request->name());
    if (it == PortBuilder::all_ports().end()) {
      return return_with_error(response, ENOENT, "No port '%s' found",
                               request->name().c_str());
    }

    ::Port::LinkStatus status = it->second->GetLinkStatus();

    response->set_speed(status.speed);
    response->set_full_duplex(status.full_duplex);
    response->set_autoneg(status.autoneg);
    response->set_link_up(status.link_up);

    return Status::OK;
  }

  Status ResetModules(ServerContext*, const EmptyRequest*,
                      EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    ModuleBuilder::DestroyAllModules();
    LOG(INFO) << "*** All modules have been destroyed ***";
    return Status::OK;
  }

  Status ListModules(ServerContext*, const EmptyRequest*,
                     ListModulesResponse* response) override {
    for (const auto& pair : ModuleBuilder::all_modules()) {
      const Module* m = pair.second;
      ListModulesResponse_Module* module = response->add_modules();

      module->set_name(m->name());
      module->set_mclass(m->module_builder()->class_name());
      module->set_desc(m->GetDesc());
    }
    return Status::OK;
  }

  Status CreateModule(ServerContext*, const CreateModuleRequest* request,
                      CreateModuleResponse* response) override {
    VLOG(1) << "CreateModuleRequest from client:" << std::endl
            << request->DebugString();

    if (!request->mclass().length()) {
      return return_with_error(response, EINVAL, "Missing 'mclass' field");
    }

    const auto& builders = ModuleBuilder::all_module_builders();
    const auto& it = builders.find(request->mclass());
    if (it == builders.end()) {
      return return_with_error(response, ENOENT, "No mclass '%s' found",
                               request->mclass().c_str());
    }
    const ModuleBuilder& builder = it->second;

    std::string mod_name;
    if (request->name().length()) {
      const auto& it2 = ModuleBuilder::all_modules().find(request->name());
      if (it2 != ModuleBuilder::all_modules().end()) {
        return return_with_error(response, EEXIST, "Module %s exists",
                                 request->name().c_str());
      }
      mod_name = request->name();
    } else {
      mod_name = ModuleBuilder::GenerateDefaultName(builder.class_name(),
                                                    builder.name_template());
    }

    pb_error_t* error = response->mutable_error();
    Module* module = create_module(mod_name, builder, request->arg(), error);

    if (module) {
      response->set_name(module->name());
    }

    return Status::OK;
  }

  Status DestroyModule(ServerContext*, const DestroyModuleRequest* request,
                       EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    Module* m;

    if (!request->name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    m_name = request->name().c_str();

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    m = it->second;

    ModuleBuilder::DestroyModule(m);

    return Status::OK;
  }

  Status GetModuleInfo(ServerContext*, const GetModuleInfoRequest* request,
                       GetModuleInfoResponse* response) override {
    const char* m_name;
    Module* m;

    if (!request->name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    m_name = request->name().c_str();

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    m = it->second;

    response->set_name(m->name());
    response->set_mclass(m->module_builder()->class_name());
    response->set_desc(m->GetDesc());

    collect_igates(m, response);
    collect_ogates(m, response);
    collect_metadata(m, response);

    return Status::OK;
  }

  Status ConnectModules(ServerContext*, const ConnectModulesRequest* request,
                        EmptyResponse* response) override {
    VLOG(1) << "ConnectModulesRequest from client:" << std::endl
            << request->DebugString();

    const char* m1_name;
    const char* m2_name;
    gate_idx_t ogate;
    gate_idx_t igate;

    Module* m1;
    Module* m2;

    int ret;

    m1_name = request->m1().c_str();
    m2_name = request->m2().c_str();
    ogate = request->ogate();
    igate = request->igate();

    if (!m1_name || !m2_name)
      return return_with_error(response, EINVAL, "Missing 'm1' or 'm2' field");

    const auto& it1 = ModuleBuilder::all_modules().find(request->m1());
    if (it1 == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m1_name);
    }

    m1 = it1->second;

    const auto& it2 = ModuleBuilder::all_modules().find(request->m2());
    if (it2 == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m2_name);
    }
    m2 = it2->second;

    if (is_any_worker_running()) {
      propagate_active_worker();
      if (m1->num_active_workers()) {
        return return_with_error(response, EBUSY, "Module '%s' is in use",
                                 m1_name);
      }
      if (m2->num_active_workers()) {
        return return_with_error(response, EBUSY, "Module '%s' is in use",
                                 m2_name);
      }
    }

    ret = m1->ConnectModules(ogate, m2, igate);
    if (ret < 0)
      return return_with_error(response, -ret, "Connection %s:%d->%d:%s failed",
                               m1_name, ogate, igate, m2_name);

    return Status::OK;
  }

  Status DisconnectModules(ServerContext*,
                           const DisconnectModulesRequest* request,
                           EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    gate_idx_t ogate;

    int ret;

    m_name = request->name().c_str();
    ogate = request->ogate();

    if (!request->name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    Module* m = it->second;

    ret = m->DisconnectModules(ogate);
    if (ret < 0)
      return return_with_error(response, -ret, "Disconnection %s:%d failed",
                               m_name, ogate);

    return Status::OK;
  }

  Status DumpMempool(ServerContext*,
                           const DumpMempoolRequest* request,
                           DumpMempoolResponse* response) override {
    int socket_filter = request->socket();
    socket_filter = (socket_filter == -1) ? (RTE_MAX_NUMA_NODES - 1) : socket_filter;
    int socket = (request->socket() == -1) ? 0 : socket_filter;
    for (; socket <= socket_filter; socket++) {
      struct rte_mempool *mempool = bess::get_pframe_pool_socket(socket);
      MempoolDump *dump = response->add_dumps();
      dump->set_socket(socket);
      dump->set_initialized(mempool != nullptr);
      if (mempool == nullptr) {
        continue;
      }
      struct rte_ring *ring = reinterpret_cast<struct rte_ring*>(mempool->pool_data);
      dump->set_mp_size(mempool->size);
      dump->set_mp_cache_size(mempool->cache_size);
      dump->set_mp_element_size(mempool->elt_size);
      dump->set_mp_populated_size(mempool->populated_size);
      dump->set_mp_available_count(rte_mempool_avail_count(mempool));
      dump->set_mp_in_use_count(rte_mempool_in_use_count(mempool));
      uint32_t ring_count = rte_ring_count(ring);
      uint32_t ring_free_count = rte_ring_free_count(ring);
      dump->set_ring_count(ring_count);
      dump->set_ring_free_count(ring_free_count);
      dump->set_ring_bytes(rte_ring_get_memsize(ring_count + ring_free_count));
    }
    return Status::OK;
  }

  Status ConfigureGateHook(ServerContext*,
                           const ConfigureGateHookRequest* request,
                           CommandResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    bool use_gate = true;
    gate_idx_t gate_idx = 0;
    bool is_igate =
        request->gate_case() == bess::pb::ConfigureGateHookRequest::kIgate;

    if (is_igate) {
      gate_idx = request->igate();
      use_gate = request->igate() >= 0;
    } else {
      gate_idx = request->ogate();
      use_gate = request->ogate() >= 0;
    }

    const auto factory = bess::GateHookFactory::all_gate_hook_factories().find(
        request->hook_name());
    if (factory == bess::GateHookFactory::all_gate_hook_factories().end()) {
      return return_with_error(response, ENOENT, "No such gate hook: %s",
                               request->hook_name().c_str());
    }

    if (request->module_name().length() == 0) {
      // Install this hook on all modules
      for (const auto& it : ModuleBuilder::all_modules()) {
        if (request->enable()) {
          *response =
              enable_hook_for_module(it.second, gate_idx, is_igate, use_gate,
                                     factory->second, request->arg());
        } else {
          *response = disable_hook_for_module(it.second, gate_idx, is_igate,
                                              use_gate, request->hook_name());
        }
        if (response->error().code() != 0) {
          return Status::OK;
        }
      }
      return Status::OK;
    }

    // Install this hook on the specified module
    const auto& it = ModuleBuilder::all_modules().find(request->module_name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               request->module_name().c_str());
    }
    if (request->enable()) {
      *response =
          enable_hook_for_module(it->second, gate_idx, is_igate, use_gate,
                                 factory->second, request->arg());
    } else {
      *response = disable_hook_for_module(it->second, gate_idx, is_igate,
                                          use_gate, request->hook_name());
    }

    return Status::OK;
  }

  Status KillBess(ServerContext*, const EmptyRequest*,
                  EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    LOG(WARNING) << "Halt requested by a client\n";

    CHECK(shutdown_func_ != nullptr);
    std::thread shutdown_helper([this]() {
      // Deadlock occurs when closing a gRPC server while processing a RPC.
      // Instead, we defer calling gRPC::Server::Shutdown() to a temporary
      // thread.
      shutdown_func_();
    });
    shutdown_helper.detach();

    return Status::OK;
  }

  Status ImportPlugin(ServerContext*, const ImportPluginRequest* request,
                      EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    VLOG(1) << "Loading plugin: " << request->path();
    if (!bess::bessd::LoadPlugin(request->path())) {
      return return_with_error(response, -1, "Failed loading plugin %s",
                               request->path().c_str());
    }
    return Status::OK;
  }

  Status UnloadPlugin(ServerContext*, const UnloadPluginRequest* request,
                      EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    VLOG(1) << "Unloading plugin: " << request->path();
    if (!bess::bessd::UnloadPlugin(request->path())) {
      return return_with_error(response, -1, "Failed unloading plugin %s",
                               request->path().c_str());
    }
    return Status::OK;
  }

  Status ListPlugins(ServerContext*, const EmptyRequest*,
                     ListPluginsResponse* response) override {
    auto list = bess::bessd::ListPlugins();
    for (auto& path : list) {
      response->add_paths(path);
    }
    return Status::OK;
  }

  Status ListMclass(ServerContext*, const EmptyRequest*,
                    ListMclassResponse* response) override {
    for (const auto& pair : ModuleBuilder::all_module_builders()) {
      const ModuleBuilder& builder = pair.second;
      response->add_names(builder.class_name());
    }
    return Status::OK;
  }

  Status GetMclassInfo(ServerContext*, const GetMclassInfoRequest* request,
                       GetMclassInfoResponse* response) override {
    VLOG(1) << "GetMclassInfo from client:" << std::endl
            << request->DebugString();
    if (!request->name().length()) {
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    }

    const std::string& cls_name = request->name();
    const auto& it = ModuleBuilder::all_module_builders().find(cls_name);
    if (it == ModuleBuilder::all_module_builders().end()) {
      return return_with_error(response, ENOENT, "No module class '%s' found",
                               cls_name.c_str());
    }
    const ModuleBuilder* cls = &it->second;

    response->set_name(cls->class_name());
    response->set_help(cls->help_text());
    for (const auto& cmd : cls->cmds()) {
      response->add_cmds(cmd.first);
      response->add_cmd_args(cmd.second);
    }
    return Status::OK;
  }

  Status ModuleCommand(ServerContext*, const CommandRequest* request,
                       CommandResponse* response) override {
    if (!request->name().length()) {
      return return_with_error(response, EINVAL,
                               "Missing module name field 'name'");
    }
    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               request->name().c_str());
    }

    // DPDK functions may be called, so be prepared
    ctx.SetNonWorker();

    Module* m = it->second;
    *response = m->RunCommand(request->cmd(), request->arg());
    return Status::OK;
  }

 private:
  Status AttachTc(bess::TrafficClass* c_, const bess::pb::TrafficClass& class_,
                  EmptyResponse* response) {
    std::unique_ptr<bess::TrafficClass> c(c_);
    int wid = class_.wid();

    if (class_.parent() == "") {
      if (wid != Worker::kAnyWorker &&
          (wid < 0 || wid >= Worker::kMaxWorkers)) {
        return return_with_error(response, EINVAL,
                                 "'wid' must be %d or between 0 and %d",
                                 Worker::kAnyWorker, Worker::kMaxWorkers - 1);
      }

      if ((wid != Worker::kAnyWorker && !is_worker_active(wid)) ||
          (wid == Worker::kAnyWorker && num_workers == 0)) {
        if (num_workers == 0 && (wid == 0 || wid == Worker::kAnyWorker)) {
          launch_worker(0, FLAGS_c);
        } else {
          return return_with_error(response, EINVAL, "worker:%d does not exist",
                                   wid);
        }
      }

      add_tc_to_orphan(c.release(), wid);
      return Status::OK;
    }

    if (wid != Worker::kAnyWorker) {
      return return_with_error(response, EINVAL,
                               "Both 'parent' and 'wid'"
                               "have been specified");
    }

    bess::TrafficClass* parent;
    const auto& tcs = TrafficClassBuilder::all_tcs();
    const auto& it = tcs.find(class_.parent());
    if (it == tcs.end()) {
      return return_with_error(response, ENOENT, "Parent TC '%s' not found",
                               class_.parent().c_str());
    }
    parent = it->second;

    bool fail = false;
    switch (parent->policy()) {
      case bess::POLICY_PRIORITY: {
        if (class_.arg_case() != bess::pb::TrafficClass::kPriority) {
          return return_with_error(response, EINVAL, "No priority specified");
        }
        bess::priority_t pri = class_.priority();
        if (pri == DEFAULT_PRIORITY) {
          return return_with_error(response, EINVAL, "Priority %d is reserved",
                                   DEFAULT_PRIORITY);
        }
        fail = !static_cast<bess::PriorityTrafficClass*>(parent)->AddChild(
            c.get(), pri);
        break;
      }
      case bess::POLICY_WEIGHTED_FAIR:
        if (class_.arg_case() != bess::pb::TrafficClass::kShare) {
          return return_with_error(response, EINVAL, "No share specified");
        }
        fail = !static_cast<bess::WeightedFairTrafficClass*>(parent)->AddChild(
            c.get(), class_.share());
        break;
      case bess::POLICY_ROUND_ROBIN:
        fail = !static_cast<bess::RoundRobinTrafficClass*>(parent)->AddChild(
            c.get());
        break;
      case bess::POLICY_RATE_LIMIT:
        fail = !static_cast<bess::RateLimitTrafficClass*>(parent)->AddChild(
            c.get());
        break;
      default:
        return return_with_error(response, EPERM,
                                 "Parent tc doesn't support children");
    }
    if (fail) {
      return return_with_error(response, EINVAL, "AddChild() failed");
    }
    c.release();
    return Status::OK;
  }

  bess::TrafficClass* FindTc(const bess::pb::TrafficClass& class_,
                             EmptyResponse* response) {
    bess::TrafficClass* c = nullptr;

    if (class_.name().length() != 0) {
      const char* name = class_.name().c_str();
      const auto all_tcs = TrafficClassBuilder::all_tcs();
      auto it = all_tcs.find(name);
      if (it == all_tcs.end()) {
        return_with_error(response, ENOENT, "Tc '%s' doesn't exist", name);
        return nullptr;
      }

      c = it->second;
    } else if (class_.leaf_module_name().length() != 0) {
      const std::string& module_name = class_.leaf_module_name();
      const auto& it = ModuleBuilder::all_modules().find(module_name);
      if (it == ModuleBuilder::all_modules().end()) {
        return_with_error(response, ENOENT, "No module '%s' found",
                          module_name.c_str());
        return nullptr;
      }
      Module* m = it->second;

      task_id_t tid = class_.leaf_module_taskid();
      if (tid >= MAX_TASKS_PER_MODULE) {
        return_with_error(response, EINVAL, "'taskid' must be between 0 and %d",
                          MAX_TASKS_PER_MODULE - 1);
        return nullptr;
      }

      if (tid >= m->tasks().size()) {
        return_with_error(response, ENOENT, "Task %s:%hu does not exist",
                          class_.leaf_module_name().c_str(), tid);
        return nullptr;
      }

      c = m->tasks()[tid]->GetTC();
    } else {
      return_with_error(response, EINVAL,
                        "One of 'name' or "
                        "'leaf_module_name' must be specified");
      return nullptr;
    }

    if (!c) {
      return_with_error(response, ENOENT, "Error finding TC");
    }

    return c;
  }

  // function to call to close this gRPC service.
  std::function<void()> shutdown_func_;
};

bool ApiServer::grpc_cb_set_ = false;

void ApiServer::Listen(const std::string& host, int port) {
  if (!builder_) {
    builder_ = new grpc::ServerBuilder();
  }

  std::string addr = bess::utils::Format("%s:%d", host.c_str(), port);
  LOG(INFO) << "Server listening on " << addr;

  builder_->AddListeningPort(addr, grpc::InsecureServerCredentials());
}

void ApiServer::Run() {
  class ServerCallbacks : public grpc::Server::GlobalCallbacks {
   public:
    ServerCallbacks() {}
    void PreSynchronousRequest(ServerContext*) { mutex_.lock(); }
    void PostSynchronousRequest(ServerContext*) { mutex_.unlock(); }

   private:
    std::mutex mutex_;
  };

  if (!builder_) {
    // We are not listening on any sockets. There is nothing to do.
    return;
  }

  if (!grpc_cb_set_) {
    // SetGlobalCallbacks() must be invoked only once.
    grpc_cb_set_ = true;
    // NOTE: Despite its documentation, SetGlobalCallbacks() does take the
    // ownership of the object pointer. So we just "new" and forget about it.
    grpc::Server::SetGlobalCallbacks(new ServerCallbacks());
  }

  BESSControlImpl service;
  builder_->RegisterService(&service);
  builder_->SetSyncServerOption(grpc::ServerBuilder::MAX_POLLERS, 1);

  std::unique_ptr<grpc::Server> server = builder_->BuildAndStart();
  if (server == nullptr) {
    LOG(ERROR) << "ServerBuilder::BuildAndStart() failed";
    return;
  }

  service.set_shutdown_func([&server]() { server->Shutdown(); });
  server->Wait();
}
