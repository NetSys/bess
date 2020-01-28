// Copyright (c) 2017, The Regents of the University of California.
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
#include "gate_hooks/tcpdump.h"
#include "gate_hooks/track.h"
#include "message.h"
#include "metadata.h"
#include "module.h"
#include "module_graph.h"
#include "opts.h"
#include "packet_pool.h"
#include "port.h"
#include "resume_hook.h"
#include "scheduler.h"
#include "shared_obj.h"
#include "traffic_class.h"
#include "utils/ether.h"
#include "utils/time.h"
#include "worker.h"

#include <rte_mempool.h>
#include <rte_ring.h>

using grpc::ServerContext;
using grpc::Status;

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

static inline bess::Gate* module_gate(const Module* m, bool is_igate,
                                      gate_idx_t gate_idx) {
  if (is_igate) {
    if (is_active_gate(m->igates(), gate_idx)) {
      return m->igates()[gate_idx];
    }
  } else {
    if (is_active_gate(m->ogates(), gate_idx)) {
      return m->ogates()[gate_idx];
    }
  }
  return nullptr;
}

static Status enable_hook_for_module(ConfigureGateHookResponse* response,
                                     const std::string hook_name,
                                     const Module* m, gate_idx_t gate_idx,
                                     bool is_igate, bool use_gate,
                                     const bess::GateHookBuilder& builder,
                                     const google::protobuf::Any& arg) {
  pb_error_t* error = response->mutable_error();
  if (use_gate) {
    bess::Gate* gate = module_gate(m, is_igate, gate_idx);
    if (gate == nullptr) {
      return return_with_error(
          response, ENOENT, "'%s': %cgate '%hu' does not exist",
          m->name().c_str(), is_igate ? 'i' : 'o', gate_idx);
    }

    bess::GateHook* hook =
        gate->CreateGateHook(&builder, gate, hook_name, arg, error);
    if (hook) {
      response->set_name(hook->name());
    }
    return Status::OK;
  }

  std::vector<std::string> created_hook_names;

  if (is_igate) {
    for (auto& gate : m->igates()) {
      if (!gate) {
        continue;
      }
      bess::GateHook* hook =
          gate->CreateGateHook(&builder, gate, hook_name, arg, error);
      if (error->code() != 0 || !hook) {
        // in case of failed creating gate hook, remove previously created
        // before return
        auto it = created_hook_names.begin();
        while (it != created_hook_names.end()) {
          gate->RemoveHook(*it);
          it = created_hook_names.erase(it);
        }
        return Status::OK;
      } else {
        created_hook_names.push_back(hook->name());
      }
    }
  } else {
    for (auto& gate : m->ogates()) {
      if (!gate) {
        continue;
      }
      bess::GateHook* hook =
          gate->CreateGateHook(&builder, gate, hook_name, arg, error);
      if (error->code() != 0 || !hook) {
        // in case of failed creating gate hook, remove previously created
        // before return
        auto it = created_hook_names.begin();
        while (it != created_hook_names.end()) {
          gate->RemoveHook(*it);
          it = created_hook_names.erase(it);
        }
        return Status::OK;
      } else {
        created_hook_names.push_back(hook->name());
      }
    }
  }
  return Status::OK;
}

static Status disable_hook_for_module(ConfigureGateHookResponse* response,
                                      const std::string& class_name,
                                      const std::string& hook_name,
                                      const Module* m, gate_idx_t gate_idx,
                                      bool is_igate, bool use_gate) {
  if (use_gate) {
    bess::Gate* gate = module_gate(m, is_igate, gate_idx);
    if (gate == nullptr) {
      return return_with_error(
          response, EINVAL, "'%s': %cgate '%hu' does not exist",
          m->name().c_str(), is_igate ? 'i' : 'o', gate_idx);
    }
    if (hook_name != "") {
      gate->RemoveHook(hook_name);
    } else {
      gate->RemoveHookByClass(class_name);
    }
    return Status::OK;
  }

  if (is_igate) {
    for (auto& gate : m->igates()) {
      if (!gate) {
        continue;
      }
      if (hook_name != "") {
        gate->RemoveHook(hook_name);
      } else {
        gate->RemoveHookByClass(class_name);
      }
    }
  } else {
    for (auto& gate : m->ogates()) {
      if (!gate) {
        continue;
      }
      if (hook_name != "") {
        gate->RemoveHook(hook_name);
      } else {
        gate->RemoveHookByClass(class_name);
      }
    }
  }
  return Status::OK;
}

static int collect_igates(Module* m, GetModuleInfoResponse* response) {
  for (const auto& g : m->igates()) {
    if (!g) {
      continue;
    }

    GetModuleInfoResponse_IGate* igate = response->add_igates();

    Track* t = reinterpret_cast<Track*>(g->FindHookByClass(Track::kName));

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

    for (const auto& hook : g->hooks()) {
      GetModuleInfoResponse_GateHook* hook_info = igate->add_gatehooks();
      hook_info->set_class_name(hook->class_name());
      hook_info->set_hook_name(hook->name());
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
    Track* t = reinterpret_cast<Track*>(g->FindHookByClass(Track::kName));
    if (t) {
      ogate->set_cnt(t->cnt());
      ogate->set_pkts(t->pkts());
      ogate->set_bytes(t->bytes());
      ogate->set_timestamp(get_epoch_time());
    }
    ogate->set_name(g->igate()->module()->name());
    ogate->set_igate(g->igate()->gate_idx());

    for (const auto& hook : g->hooks()) {
      GetModuleInfoResponse_GateHook* hook_info = ogate->add_gatehooks();
      hook_info->set_class_name(hook->class_name());
      hook_info->set_hook_name(hook->name());
    }
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
                           const google::protobuf::Any& arg, pb_error_t* perr) {
  std::unique_ptr<::Port> p;

  if (num_inc_q == 0) {
    num_inc_q = 1;
  }

  if (num_out_q == 0) {
    num_out_q = 1;
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
      p.reset(PortBuilder::all_ports().at(name));
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

  p->num_queues[PACKET_DIR_INC] = num_inc_q;
  p->num_queues[PACKET_DIR_OUT] = num_out_q;
  p->queue_size[PACKET_DIR_INC] = size_inc_q;
  p->queue_size[PACKET_DIR_OUT] = size_out_q;

  // DPDK functions may be called, so be prepared
  current_worker.SetNonWorker();

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
  } else if (c->policy() == bess::POLICY_LEAF) {
    const bess::LeafTrafficClass* leaf =
        static_cast<const bess::LeafTrafficClass*>(c);
    const Task* task = leaf->task();
    const Module* module = task->module();

    status->mutable_class_()->set_leaf_module_name(task->module()->name());

    auto it = std::find(module->tasks().begin(), module->tasks().end(), task);
    CHECK(it != module->tasks().end());
    uint64_t task_id = it - module->tasks().begin();
    status->mutable_class_()->set_leaf_module_taskid(task_id);
  }
}

class BESSControlImpl final : public BESSControl::Service {
 public:
  void set_shutdown_func(const std::function<void()>& func) {
    shutdown_func_ = func;
  }

  Status GetVersion(ServerContext*, const EmptyRequest*,
                    VersionResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    response->set_version(google::VersionString());
    return Status::OK;
  }

  Status ResetAll(ServerContext* context, const EmptyRequest* request,
                  EmptyResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    Status status;
    WorkerPauser wp;

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    pause_all_workers();
    LOG(INFO) << "*** All workers have been paused ***";
    return Status::OK;
  }

  Status PauseWorker(ServerContext*, const PauseWorkerRequest* req,
                     EmptyResponse*) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!is_any_worker_running()) {
      attach_orphans();
    }

    bess::run_global_resume_hooks();

    LOG(INFO) << "*** Resuming ***";
    resume_all_workers();
    return Status::OK;
  }

  Status ResumeWorker(ServerContext*, const ResumeWorkerRequest* req,
                      EmptyResponse*) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    int wid = req->wid();
    LOG(INFO) << "*** Resuming worker " << wid << " ***";
    resume_worker(wid);
    return Status::OK;
  }

  Status ResetWorkers(ServerContext*, const EmptyRequest*,
                      EmptyResponse*) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;
    destroy_all_workers();
    LOG(INFO) << "*** All workers have been destroyed ***";
    return Status::OK;
  }

  Status ListWorkers(ServerContext*, const EmptyRequest*,
                     ListWorkersResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
      for (const auto& it : TrafficClassBuilder::all_tcs()) {
        bess::TrafficClass* c = it.second;
        if (c->policy() == bess::POLICY_LEAF && c->Root() == root) {
          return return_with_error(response, EBUSY,
                                   "Worker %d has active tasks: %s", wid,
                                   c->name().c_str());
        }
      }
    }

    destroy_worker(wid);
    return Status::OK;
  }

  Status ResetTcs(ServerContext*, const EmptyRequest*,
                  EmptyResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;

    if (!TrafficClassBuilder::ClearAll()) {
      return return_with_error(response, EBUSY, "TCs still have tasks");
    }

    return Status::OK;
  }

  Status ListTcs(ServerContext*, const ListTcsRequest* request,
                 ListTcsResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    int wid_filter = request->wid();
    if (wid_filter >= Worker::kMaxWorkers) {
      return return_with_error(response, EINVAL,
                               "'wid' must be between 0 and %d",
                               Worker::kMaxWorkers - 1);
    } else if (wid_filter < 0) {
      wid_filter = Worker::kAnyWorker;
    }

    for (const auto& tc_pair : TrafficClassBuilder::all_tcs()) {
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // Start by attaching orphans -- this is essential to make sure we visit
    // every TC.
    if (!is_any_worker_running()) {
      // If any worker is running (i.e., not everything is paused), then there
      // is no point in attaching orphans.
      attach_orphans();
    }
    ModuleGraph::PropagateActiveWorker();
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

      for (const auto& tc_pair : TrafficClassBuilder::all_tcs()) {
        bess::TrafficClass* c = tc_pair.second;
        if (c->policy() == bess::POLICY_LEAF && root == c->Root()) {
          auto leaf = static_cast<bess::LeafTrafficClass*>(c);
          int constraints = leaf->task()->GetSocketConstraints();
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
    for (const auto& pair : ModuleGraph::GetAllModules()) {
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (const auto& pair : PortBuilder::all_port_builders()) {
      const PortBuilder& builder = pair.second;
      response->add_driver_names(builder.class_name());
    }

    return Status::OK;
  }

  Status GetDriverInfo(ServerContext*, const GetDriverInfoRequest* request,
                       GetDriverInfoResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (const auto& pair : PortBuilder::all_ports()) {
      const ::Port* p = pair.second;
      bess::pb::ListPortsResponse::Port* port = response->add_ports();

      port->set_name(p->name());
      port->set_driver(p->port_builder()->class_name());
      port->set_mac_addr(p->conf().mac_addr.ToString());
      port->set_num_inc_q(p->num_rx_queues());
      port->set_num_out_q(p->num_tx_queues());
      port->set_size_inc_q(p->rx_queue_size());
      port->set_size_out_q(p->tx_queue_size());
      *port->mutable_driver_arg() = p->driver_arg();
    }

    return Status::OK;
  }

  Status CreatePort(ServerContext*, const CreatePortRequest* request,
                    CreatePortResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
                       request->size_out_q(), request->arg(), error);

    if (!port)
      return Status::OK;

    response->set_name(port->name());
    response->set_mac_addr(port->conf().mac_addr.ToString());

    return Status::OK;
  }

  Status SetPortConf(ServerContext*, const SetPortConfRequest* request,
                     CommandResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!request->name().length()) {
      return return_with_error(response, EINVAL, "Port name is not given");
    }

    const char* port_name = request->name().c_str();
    const auto& it = PortBuilder::all_ports().find(port_name);
    if (it == PortBuilder::all_ports().end()) {
      return return_with_error(response, ENOENT, "No port `%s' found",
                               port_name);
    }

    const bess::pb::PortConf& pb_conf = request->conf();
    Port::Conf conf;

    conf.mtu = pb_conf.mtu();
    conf.admin_up = pb_conf.admin_up();

    if (!conf.mac_addr.FromString(pb_conf.mac_addr())) {
      return return_with_error(
          response, EINVAL,
          "MAC address should be formatted xx:xx:xx:xx:xx:xx");
    }

    WorkerPauser wp;
    *response = it->second->UpdateConf(conf);
    return Status::OK;
  }

  Status GetPortConf(ServerContext*, const GetPortConfRequest* request,
                     GetPortConfResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!request->name().length()) {
      return return_with_error(response, EINVAL, "Port name is not given");
    }

    const char* port_name = request->name().c_str();
    const auto& it = PortBuilder::all_ports().find(port_name);
    if (it == PortBuilder::all_ports().end()) {
      return return_with_error(response, ENOENT, "No port `%s' found",
                               port_name);
    }

    Port::Conf conf = it->second->conf();
    bess::pb::PortConf* pb_conf = response->mutable_conf();

    pb_conf->set_mac_addr(conf.mac_addr.ToString());
    pb_conf->set_mtu(conf.mtu);
    pb_conf->set_admin_up(conf.admin_up);

    return Status::OK;
  }

  Status DestroyPort(ServerContext*, const DestroyPortRequest* request,
                     EmptyResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const auto& it = PortBuilder::all_ports().find(request->name());
    if (it == PortBuilder::all_ports().end()) {
      return return_with_error(response, ENOENT, "No port '%s' found",
                               request->name().c_str());
    }

    ::Port::PortStats stats = it->second->GetPortStats();

    response->mutable_inc()->set_packets(stats.inc.packets);
    response->mutable_inc()->set_dropped(stats.inc.dropped);
    response->mutable_inc()->set_bytes(stats.inc.bytes);
    *response->mutable_inc()->mutable_requested_hist() = {
        stats.inc.requested_hist.begin(), stats.inc.requested_hist.end()};
    *response->mutable_inc()->mutable_actual_hist() = {
        stats.inc.actual_hist.begin(), stats.inc.actual_hist.end()};
    *response->mutable_inc()->mutable_diff_hist() = {
        stats.inc.diff_hist.begin(), stats.inc.diff_hist.end()};

    response->mutable_out()->set_packets(stats.out.packets);
    response->mutable_out()->set_dropped(stats.out.dropped);
    response->mutable_out()->set_bytes(stats.out.bytes);
    *response->mutable_out()->mutable_requested_hist() = {
        stats.out.requested_hist.begin(), stats.out.requested_hist.end()};
    *response->mutable_out()->mutable_actual_hist() = {
        stats.out.actual_hist.begin(), stats.out.actual_hist.end()};
    *response->mutable_out()->mutable_diff_hist() = {
        stats.out.diff_hist.begin(), stats.out.diff_hist.end()};

    response->set_timestamp(get_epoch_time());

    return Status::OK;
  }

  Status GetLinkStatus(ServerContext*, const GetLinkStatusRequest* request,
                       GetLinkStatusResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
                      EmptyResponse*) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;

    ModuleGraph::DestroyAllModules();
    bess::event_modules.clear();
    LOG(INFO) << "*** All modules have been destroyed ***";
    return Status::OK;
  }

  Status ListModules(ServerContext*, const EmptyRequest*,
                     ListModulesResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (const auto& pair : ModuleGraph::GetAllModules()) {
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
      const auto& it2 = ModuleGraph::GetAllModules().find(request->name());
      if (it2 != ModuleGraph::GetAllModules().end()) {
        return return_with_error(response, EEXIST, "Module %s exists",
                                 request->name().c_str());
      }
      mod_name = request->name();
    } else {
      mod_name = ModuleGraph::GenerateDefaultName(builder.class_name(),
                                                  builder.name_template());
    }

    // DPDK functions may be called, so be prepared
    current_worker.SetNonWorker();

    pb_error_t* error = response->mutable_error();
    Module* module =
        ModuleGraph::CreateModule(builder, mod_name, request->arg(), error);
    if (module) {
      response->set_name(module->name());
      bess::event_modules[bess::Event::PreResume].insert(module);
    }

    return Status::OK;
  }

  Status DestroyModule(ServerContext*, const DestroyModuleRequest* request,
                       EmptyResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;
    const char* m_name;
    Module* m;

    if (!request->name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    m_name = request->name().c_str();

    const auto& it = ModuleGraph::GetAllModules().find(request->name());
    if (it == ModuleGraph::GetAllModules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    m = it->second;

    auto& resume_modules = bess::event_modules[bess::Event::PreResume];
    if (resume_modules.erase(m) > 0) {
      VLOG(1) << "Cleared pre-resume hook for module '" << m->name() << "'";
    }

    ModuleGraph::DestroyModule(m);

    return Status::OK;
  }

  Status GetModuleInfo(ServerContext*, const GetModuleInfoRequest* request,
                       GetModuleInfoResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const char* m_name;
    Module* m;

    if (!request->name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    m_name = request->name().c_str();

    const auto& it = ModuleGraph::GetAllModules().find(request->name());
    if (it == ModuleGraph::GetAllModules().end()) {
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
    response->set_deadends(m->deadends());

    return Status::OK;
  }

  Status ConnectModules(ServerContext*, const ConnectModulesRequest* request,
                        EmptyResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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

    const auto& it1 = ModuleGraph::GetAllModules().find(request->m1());
    if (it1 == ModuleGraph::GetAllModules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m1_name);
    }

    m1 = it1->second;

    const auto& it2 = ModuleGraph::GetAllModules().find(request->m2());
    if (it2 == ModuleGraph::GetAllModules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m2_name);
    }
    m2 = it2->second;

    if (is_any_worker_running()) {
      ModuleGraph::PropagateActiveWorker();
      if (m1->num_active_workers() || m2->num_active_workers()) {
        WorkerPauser wp;  // Only pause when absolutely required
        ret = ModuleGraph::ConnectModules(m1, ogate, m2, igate,
                                          request->skip_default_hooks());
        goto done;
      }
    }
    ret = ModuleGraph::ConnectModules(m1, ogate, m2, igate,
                                      request->skip_default_hooks());
  done:
    if (ret < 0)
      return return_with_error(response, -ret, "Connection %s:%d->%d:%s failed",
                               m1_name, ogate, igate, m2_name);

    return Status::OK;
  }

  Status DisconnectModules(ServerContext*,
                           const DisconnectModulesRequest* request,
                           EmptyResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;
    const char* m_name;
    gate_idx_t ogate;

    int ret;

    m_name = request->name().c_str();
    ogate = request->ogate();

    if (!request->name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    const auto& it = ModuleGraph::GetAllModules().find(request->name());
    if (it == ModuleGraph::GetAllModules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    Module* m = it->second;

    ret = ModuleGraph::DisconnectModule(m, ogate);
    if (ret < 0)
      return return_with_error(response, -ret, "Disconnection %s:%d failed",
                               m_name, ogate);

    return Status::OK;
  }

  Status DumpMempool(ServerContext*, const DumpMempoolRequest* request,
                     DumpMempoolResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    int socket_filter = request->socket();
    socket_filter =
        (socket_filter == -1) ? (RTE_MAX_NUMA_NODES - 1) : socket_filter;
    int socket = (request->socket() == -1) ? 0 : socket_filter;
    for (; socket <= socket_filter; socket++) {
      bess::PacketPool* pool = bess::PacketPool::GetDefaultPool(socket);
      if (!pool) {
        continue;
      }

      rte_mempool* mempool = pool->pool();
      MempoolDump* dump = response->add_dumps();
      dump->set_socket(socket);
      dump->set_initialized(mempool != nullptr);
      if (mempool == nullptr) {
        continue;
      }
      struct rte_ring* ring =
          reinterpret_cast<struct rte_ring*>(mempool->pool_data);
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

  Status ListGateHookClass(ServerContext*, const EmptyRequest*,
                           ListGateHookClassResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (const auto& pair : bess::GateHookBuilder::all_gate_hook_builders()) {
      const auto& builder = pair.second;
      response->add_names(builder.class_name());
    }
    return Status::OK;
  }

  Status GetGateHookClassInfo(ServerContext*,
                              const GetGateHookClassInfoRequest* request,
                              GetGateHookClassInfoResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    VLOG(1) << "GetGateHookClassInfo from client:" << std::endl
            << request->DebugString();
    if (!request->name().length()) {
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    }

    const std::string& cls_name = request->name();
    const auto& it =
        bess::GateHookBuilder::all_gate_hook_builders().find(cls_name);
    if (it == bess::GateHookBuilder::all_gate_hook_builders().end()) {
      return return_with_error(response, ENOENT, "No gatehook class '%s' found",
                               cls_name.c_str());
    }
    const bess::GateHookBuilder* cls = &it->second;

    response->set_name(cls->class_name());
    response->set_help(cls->help_text());
    for (const auto& cmd : cls->cmds()) {
      response->add_cmds(cmd.first);
      response->add_cmd_args(cmd.second);
    }
    return Status::OK;
  }

  Status ListGateHooks(ServerContext*, const EmptyRequest*,
                       ListGateHooksResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (const auto& pair : ModuleGraph::GetAllModules()) {
      const Module* m = pair.second;
      for (auto& gate : m->igates()) {
        if (!gate) {
          continue;
        }
        for (auto& hook : gate->hooks()) {
          GateHookInfo* info = response->add_hooks();
          info->set_class_name(hook->class_name());
          info->set_hook_name(hook->name());
          info->set_module_name(m->name());
          info->set_igate(gate->gate_idx());
          *(info->mutable_arg()) = hook->arg();
        }
      }
      for (auto& gate : m->ogates()) {
        if (!gate) {
          continue;
        }
        for (auto& hook : gate->hooks()) {
          GateHookInfo* info = response->add_hooks();
          info->set_class_name(hook->class_name());
          info->set_hook_name(hook->name());
          info->set_module_name(m->name());
          info->set_ogate(gate->gate_idx());
          *(info->mutable_arg()) = hook->arg();
        }
      }
    }
    return Status::OK;
  }

  Status ConfigureGateHook(ServerContext*,
                           const ConfigureGateHookRequest* request,
                           ConfigureGateHookResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;
    bool use_gate = true;
    gate_idx_t gate_idx = 0;
    bool is_igate =
        request->hook().gate_case() == bess::pb::GateHookInfo::kIgate;

    if (is_igate) {
      gate_idx = request->hook().igate();
      use_gate = request->hook().igate() >= 0;
    } else {
      gate_idx = request->hook().ogate();
      use_gate = request->hook().ogate() >= 0;
    }

    const auto builder = bess::GateHookBuilder::all_gate_hook_builders().find(
        request->hook().class_name());
    if (builder == bess::GateHookBuilder::all_gate_hook_builders().end()) {
      return return_with_error(response, ENOENT, "No such gate hook: %s",
                               request->hook().class_name().c_str());
    }

    if (request->hook().module_name().length() == 0) {
      // Install this hook on all modules
      for (const auto& it : ModuleGraph::GetAllModules()) {
        if (request->enable()) {
          enable_hook_for_module(response, request->hook().hook_name(),
                                 it.second, gate_idx, is_igate, use_gate,
                                 builder->second, request->hook().arg());
        } else {
          disable_hook_for_module(response, request->hook().class_name(),
                                  request->hook().hook_name(), it.second,
                                  gate_idx, is_igate, use_gate);
        }
        if (response->error().code() != 0) {
          return Status::OK;
        }
      }
      return Status::OK;
    }

    // Install this hook on the specified module
    const auto& it =
        ModuleGraph::GetAllModules().find(request->hook().module_name());
    if (it == ModuleGraph::GetAllModules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               request->hook().module_name().c_str());
    }
    if (request->enable()) {
      enable_hook_for_module(response, request->hook().hook_name(), it->second,
                             gate_idx, is_igate, use_gate, builder->second,
                             request->hook().arg());
    } else {
      disable_hook_for_module(response, request->hook().class_name(),
                              request->hook().hook_name(), it->second, gate_idx,
                              is_igate, use_gate);
    }

    return Status::OK;
  }

  Status GateHookCommand(ServerContext*, const GateHookCommandRequest* request,
                         CommandResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    // No need to look up the hook builder: the gate either
    // has a hook instance with the right name, or doesn't.
    const bess::pb::GateHookInfo& rh = request->hook();
    const auto& it = ModuleGraph::GetAllModules().find(rh.module_name());
    if (it == ModuleGraph::GetAllModules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               rh.module_name().c_str());
    }
    Module* m = it->second;
    bool is_igate = rh.gate_case() == bess::pb::GateHookInfo::kIgate;
    gate_idx_t gate_idx = is_igate ? rh.igate() : rh.ogate();
    bess::Gate* g = module_gate(m, is_igate, gate_idx);
    if (g == nullptr) {
      return return_with_error(
          response, EINVAL, "%s: %cgate '%hu' does not exist",
          m->name().c_str(), is_igate ? 'i' : 'o', gate_idx);
    }

    bess::GateHook* hook = g->FindHook(rh.hook_name());
    if (hook == nullptr) {
      return return_with_error(response, ENOENT,
                               "%s: %cgate '%hu' has no hook named '%s'",
                               m->name().c_str(), is_igate ? 'i' : 'o',
                               gate_idx, rh.hook_name().c_str());
    }

    // DPDK functions may be called, so be prepared
    current_worker.SetNonWorker();

    *response = hook->RunCommand(request->cmd(), rh.arg());
    return Status::OK;
  }

  Status ConfigureResumeHook(ServerContext*,
                             const ConfigureResumeHookRequest* request,
                             CommandResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto& hooks = bess::global_resume_hooks;
    auto hook_it = hooks.end();
    for (auto it = hooks.begin(); it != hooks.end(); ++it) {
      if (it->get()->name() == request->hook_name()) {
        hook_it = it;
        break;
      }
    }

    if (!request->enable()) {
      if (hook_it != hooks.end()) {
        hooks.erase(hook_it);
      }
      return Status::OK;
    }

    if (hook_it != hooks.end()) {
      return return_with_error(response, EEXIST,
                               "Resume hook '%s' is already installed",
                               request->hook_name().c_str());
    }

    const auto builder =
        bess::ResumeHookBuilder::all_resume_hook_builders().find(
            request->hook_name());
    if (builder == bess::ResumeHookBuilder::all_resume_hook_builders().end()) {
      return return_with_error(response, ENOENT, "No such resume hook '%s'",
                               request->hook_name().c_str());
    }
    auto hook = builder->second.CreateResumeHook();
    *response = builder->second.InitResumeHook(hook.get(), request->arg());
    if (response->has_error()) {
      return Status::OK;
    }
    hooks.insert(std::move(hook));

    return Status::OK;
  }

  Status KillBess(ServerContext*, const EmptyRequest*,
                  EmptyResponse*) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;
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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;
    VLOG(1) << "Loading plugin: " << request->path();
    if (!bess::bessd::LoadPlugin(request->path())) {
      return return_with_error(response, -1, "Failed loading plugin %s",
                               request->path().c_str());
    }
    return Status::OK;
  }

  Status UnloadPlugin(ServerContext*, const UnloadPluginRequest* request,
                      EmptyResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    WorkerPauser wp;

    VLOG(1) << "Unloading plugin: " << request->path();
    if (!bess::bessd::UnloadPlugin(request->path())) {
      return return_with_error(response, -1, "Failed unloading plugin %s",
                               request->path().c_str());
    }
    return Status::OK;
  }

  Status ListPlugins(ServerContext*, const EmptyRequest*,
                     ListPluginsResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    auto list = bess::bessd::ListPlugins();
    for (auto& path : list) {
      response->add_paths(path);
    }
    return Status::OK;
  }

  Status ListMclass(ServerContext*, const EmptyRequest*,
                    ListMclassResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    for (const auto& pair : ModuleBuilder::all_module_builders()) {
      const ModuleBuilder& builder = pair.second;
      response->add_names(builder.class_name());
    }
    return Status::OK;
  }

  Status GetMclassInfo(ServerContext*, const GetMclassInfoRequest* request,
                       GetMclassInfoResponse* response) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

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
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!request->name().length()) {
      return return_with_error(response, EINVAL,
                               "Missing module name field 'name'");
    }
    const auto& it = ModuleGraph::GetAllModules().find(request->name());
    if (it == ModuleGraph::GetAllModules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               request->name().c_str());
    }

    // DPDK functions may be called, so be prepared
    current_worker.SetNonWorker();

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
      const auto& it = ModuleGraph::GetAllModules().find(module_name);
      if (it == ModuleGraph::GetAllModules().end()) {
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

  // gRPC service handlers are not thread-safe; we serialize them with a lock.
  // A recursive mutex is required since handlers may call each other.
  std::recursive_mutex mutex_;
};

void ApiServer::Listen(const std::string& addr) {
  if (!builder_) {
    builder_ = new grpc::ServerBuilder();
  }

  LOG(INFO) << "Server listening on " << addr;

  builder_->AddListeningPort(addr, grpc::InsecureServerCredentials());
}

void ApiServer::Run() {
  if (!builder_) {
    // We are not listening on any sockets. There is nothing to do.
    return;
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
