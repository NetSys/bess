#include "bessctl.h"

#include <future>
#include <map>
#include <string>
#include <thread>

#include <glog/logging.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "service.grpc.pb.h"
#pragma GCC diagnostic pop

#include "bessd.h"
#include "gate.h"
#include "hooks/track.h"
#include "message.h"
#include "metadata.h"
#include "module.h"
#include "opts.h"
#include "port.h"
#include "scheduler.h"
#include "traffic_class.h"
#include "utils/ether.h"
#include "utils/format.h"
#include "utils/time.h"
#include "worker.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::ServerContext;
using grpc::ServerBuilder;

using bess::priority_t;
using bess::resource_t;
using bess::resource_share_t;
using bess::PriorityTrafficClass;
using bess::TrafficClassBuilder;
using namespace bess::pb;

static std::promise<void> exit_requested;

template <typename T>
static inline Status return_with_error(T* response, int code, const char* fmt,
                                       ...) {
  va_list ap;
  va_start(ap, fmt);
  response->mutable_error()->set_err(code);
  response->mutable_error()->set_errmsg(bess::utils::FormatVarg(fmt, ap));
  va_end(ap);
  return Status::OK;
}

template <typename T>
static inline Status return_with_errno(T* response, int code) {
  response->mutable_error()->set_err(code);
  response->mutable_error()->set_errmsg(strerror(code));
  return Status::OK;
}

static pb_error_t enable_track_for_module(const Module* m, gate_idx_t gate_idx,
                                          bool is_igate, bool use_gate) {
  int ret;

  if (use_gate) {
    if (!is_igate && gate_idx >= m->ogates().size()) {
      return pb_error(EINVAL, "Output gate '%hu' does not exist", gate_idx);
    }

    if (is_igate && gate_idx >= m->igates().size()) {
      return pb_error(EINVAL, "Input gate '%hu' does not exist", gate_idx);
    }

    if (is_igate && (ret = m->igates()[gate_idx]->AddHook(new TrackGate()))) {
      return pb_error(ret, "Failed to track input gate '%hu'", gate_idx);
    }

    if ((ret = m->ogates()[gate_idx]->AddHook(new TrackGate()))) {
      return pb_error(ret, "Failed to track output gate '%hu'", gate_idx);
    }
  }

  if (is_igate) {
    for (auto& gate : m->igates()) {
      if ((ret = gate->AddHook(new TrackGate()))) {
        return pb_error(ret, "Failed to track input gate '%hu'",
                        gate->gate_idx());
      }
    }
  } else {
    for (auto& gate : m->ogates()) {
      if ((ret = gate->AddHook(new TrackGate()))) {
        return pb_error(ret, "Failed to track output gate '%hu'",
                        gate->gate_idx());
      }
    }
  }
  return pb_errno(0);
}

static pb_error_t disable_track_for_module(const Module* m, gate_idx_t gate_idx,
                                           bool is_igate, bool use_gate) {
  if (use_gate) {
    if (!is_igate && gate_idx >= m->ogates().size()) {
      return pb_error(EINVAL, "Output gate '%hu' does not exist", gate_idx);
    }

    if (is_igate && gate_idx >= m->igates().size()) {
      return pb_error(EINVAL, "Input gate '%hu' does not exist", gate_idx);
    }

    if (is_igate) {
      m->igates()[gate_idx]->RemoveHook(kGateHookTrackGate);
      return pb_errno(0);
    }
    m->ogates()[gate_idx]->RemoveHook(kGateHookTrackGate);
    return pb_errno(0);
  }

  if (is_igate) {
    for (auto& gate : m->igates()) {
      gate->RemoveHook(kGateHookTrackGate);
    }
  } else {
    for (auto& gate : m->ogates()) {
      gate->RemoveHook(kGateHookTrackGate);
    }
  }
  return pb_errno(0);
}

static int collect_igates(Module* m, GetModuleInfoResponse* response) {
  for (const auto& g : m->igates()) {
    if (!g) {
      continue;
    }

    GetModuleInfoResponse_IGate* igate = response->add_igates();

    TrackGate* t =
        reinterpret_cast<TrackGate*>(g->FindHook(kGateHookTrackGate));

    if (t) {
      igate->set_cnt(t->cnt());
      igate->set_pkts(t->pkts());
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
    TrackGate* t =
        reinterpret_cast<TrackGate*>(g->FindHook(kGateHookTrackGate));
    if (t) {
      ogate->set_cnt(t->cnt());
      ogate->set_pkts(t->pkts());
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

  bess::utils::EthHeader::Address mac_addr;

  if (mac_addr_str.length() > 0) {
    if (!mac_addr.FromString(mac_addr_str)) {
      perr->set_err(EINVAL);
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
    perr->set_err(EINVAL);
    perr->set_errmsg("Invalid number of queues");
    return nullptr;
  }

  if (size_inc_q > MAX_QUEUE_SIZE || size_out_q > MAX_QUEUE_SIZE) {
    perr->set_err(EINVAL);
    perr->set_errmsg("Invalid queue size");
    return nullptr;
  }

  std::string port_name;

  if (name.length() > 0) {
    if (PortBuilder::all_ports().count(name)) {
      perr->set_err(EEXIST);
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

  memcpy(p->mac_addr, mac_addr.bytes, ETH_ALEN);
  p->num_queues[PACKET_DIR_INC] = num_inc_q;
  p->num_queues[PACKET_DIR_OUT] = num_out_q;
  p->queue_size[PACKET_DIR_INC] = size_inc_q;
  p->queue_size[PACKET_DIR_OUT] = size_out_q;

  // DPDK functions may be called, so be prepared
  ctx.SetNonWorker();

  *perr = p->InitWithGenericArg(arg);
  if (perr->err() != 0) {
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
  *perr = m->InitWithGenericArg(arg);
  if (perr->err() != 0) {
    VLOG(1) << perr->DebugString();
    ModuleBuilder::DestroyModule(m);
    return nullptr;
  }

  if (!ModuleBuilder::AddModule(m)) {
    *perr = pb_errno(ENOMEM);
    return nullptr;
  }
  return m;
}

class BESSControlImpl final : public BESSControl::Service {
 public:
  Status ResetAll(ServerContext* context, const EmptyRequest* request,
                  EmptyResponse* response) override {
    Status status;

    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    LOG(INFO) << "*** ResetAll requested ***";

    status = ResetModules(context, request, response);
    if (response->error().err() != 0) {
      return status;
    }

    status = ResetPorts(context, request, response);
    if (response->error().err() != 0) {
      return status;
    }

    status = ResetTcs(context, request, response);
    if (response->error().err() != 0) {
      return status;
    }

    status = ResetWorkers(context, request, response);
    if (response->error().err() != 0) {
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

  Status ResumeAll(ServerContext*, const EmptyRequest*,
                   EmptyResponse*) override {
    LOG(INFO) << "*** Resuming ***";
    resume_all_workers();
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
    for (int wid = 0; wid < MAX_WORKERS; wid++) {
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
    if (wid >= MAX_WORKERS) {
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
    launch_worker(wid, core);
    return Status::OK;
  }

  Status DestroyWorker(ServerContext*, const DestroyWorkerRequest* request,
                       EmptyResponse* response) override {
    uint64_t wid = request->wid();
    if (wid >= MAX_WORKERS) {
      return return_with_error(response, EINVAL, "Invalid worker id");
    }
    Worker* worker = workers[wid];
    if (!worker) {
      return return_with_error(response, ENOENT, "Worker %d is not active",
                               wid);
    }

    bess::TrafficClass* root = workers[wid]->scheduler()->root();
    if (root) {
      bool has_tasks = false;
      root->Traverse(
          [](const bess::TrafficClass* c, void* arg) {
            bool have_tasks = false;
            if (c->policy() == bess::POLICY_LEAF) {
              have_tasks = reinterpret_cast<const bess::LeafTrafficClass*>(c)
                               ->tasks()
                               .size() > 0;
            }
            *reinterpret_cast<bool*>(arg) |= have_tasks;
          },
          static_cast<void*>(&has_tasks));
      if (has_tasks) {
        return return_with_error(response, EBUSY, "Worker %d has active tasks ",
                                 wid);
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
    int wid_filter;
    int i;

    wid_filter = request->wid();
    if (wid_filter >= num_workers) {
      return return_with_error(
          response, EINVAL, "'wid' must be between 0 and %d", num_workers - 1);
    }

    if (wid_filter < 0) {
      i = 0;
      wid_filter = num_workers - 1;
    } else {
      i = wid_filter;
    }

    struct traverse_arg {
      ListTcsResponse* response;
      int wid;
    };

    for (; i <= wid_filter; i++) {
      const bess::TrafficClass* root = workers[i]->scheduler()->root();
      if (!root) {
        return return_with_error(response, ENOENT, "worker:%d has no root tc",
                                 i);
      }

      struct traverse_arg arg__ = {response, i};

      root->Traverse(
          [](const bess::TrafficClass* c, void* arg) {
            struct traverse_arg* arg_ =
                reinterpret_cast<struct traverse_arg*>(arg);

            ListTcsResponse_TrafficClassStatus* status =
                arg_->response->add_classes_status();

            if (c->parent()) {
              status->set_parent(c->parent()->name());
            }

            status->mutable_class_()->set_name(c->name());
            status->mutable_class_()->set_blocked(c->blocked());

            if (c->policy() >= 0 && c->policy() < bess::NUM_POLICIES) {
              status->mutable_class_()->set_policy(
                  bess::TrafficPolicyName[c->policy()]);
            } else {
              status->mutable_class_()->set_policy("invalid");
            }

            if (c->policy() == bess::POLICY_LEAF) {
              status->set_tasks(
                  reinterpret_cast<const bess::LeafTrafficClass*>(c)
                      ->tasks()
                      .size());
            }

            status->mutable_class_()->set_wid(arg_->wid);

            if (c->policy() == bess::POLICY_RATE_LIMIT) {
              const bess::RateLimitTrafficClass* rl =
                  reinterpret_cast<const bess::RateLimitTrafficClass*>(c);
              std::string resource = bess::ResourceName.at(rl->resource());
              int64_t limit = rl->limit_arg();
              int64_t max_burst = rl->max_burst_arg();
              status->mutable_class_()->mutable_limit()->insert(
                  {resource, limit});
              status->mutable_class_()->mutable_max_burst()->insert(
                  {resource, max_burst});
            }
          },
          static_cast<void*>(&arg__));
    }

    return Status::OK;
  }

  Status AddTc(ServerContext*, const AddTcRequest* request,
               EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    int wid;

    const char* tc_name = request->class_().name().c_str();
    if (request->class_().name().length() == 0) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    }

    if (TrafficClassBuilder::all_tcs().count(tc_name)) {
      return return_with_error(response, EINVAL, "Name '%s' already exists",
                               tc_name);
    }

    wid = request->class_().wid();
    if (wid >= MAX_WORKERS) {
      return return_with_error(
          response, EINVAL, "'wid' must be between 0 and %d", MAX_WORKERS - 1);
    }

    if (!is_worker_active(wid)) {
      if (num_workers == 0 && wid == 0) {
        launch_worker(wid, FLAGS_c);
      } else {
        return return_with_error(response, EINVAL, "worker:%d does not exist",
                                 wid);
      }
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
      c = reinterpret_cast<bess::TrafficClass*>(
          TrafficClassBuilder::CreateTrafficClass<bess::LeafTrafficClass>(
              tc_name));
    } else {
      return return_with_error(response, EINVAL, "Invalid traffic policy");
    }

    if (!c) {
      return return_with_error(response, ENOMEM, "CreateTrafficClass failed");
    }

    bess::TrafficClass* root;
    if (request->class_().parent().length() == 0) {
      root = workers[wid]->scheduler()->root();
    } else {
      const auto& tcs = TrafficClassBuilder::all_tcs();
      const auto& it = tcs.find(request->class_().parent());
      if (it == tcs.end()) {
        return return_with_error(response, ENOENT, "Parent TC '%s' not found",
                                 tc_name);
      }
      root = it->second;
    }

    bool fail = false;
    switch (root->policy()) {
      case bess::POLICY_PRIORITY: {
        if (request->class_().arg_case() != bess::pb::TrafficClass::kPriority) {
          return return_with_error(response, EINVAL, "No priority specified");
        }
        priority_t pri = request->class_().priority();
        if (pri == DEFAULT_PRIORITY) {
          return return_with_error(response, EINVAL, "Priority %d is reserved",
                                   DEFAULT_PRIORITY);
        }
        fail = !reinterpret_cast<bess::PriorityTrafficClass*>(root)->AddChild(
            c, pri);
        break;
      }
      case bess::POLICY_WEIGHTED_FAIR:
        if (request->class_().arg_case() != bess::pb::TrafficClass::kShare) {
          return return_with_error(response, EINVAL, "No share specified");
        }
        fail =
            !reinterpret_cast<bess::WeightedFairTrafficClass*>(root)->AddChild(
                c, request->class_().share());
        break;
      case bess::POLICY_ROUND_ROBIN:
        fail =
            !reinterpret_cast<bess::RoundRobinTrafficClass*>(root)->AddChild(c);
        break;
      case bess::POLICY_RATE_LIMIT:
        fail =
            !reinterpret_cast<bess::RateLimitTrafficClass*>(root)->AddChild(c);
        break;
      default:
        return return_with_error(response, EPERM,
                                 "Root tc doens't support children");
    }
    if (fail) {
      return return_with_error(response, EINVAL, "AddChild() failed");
    }

    return Status::OK;
  }

  Status UpdateTc(ServerContext*, const UpdateTcRequest* request,
                  EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    const char* tc_name = request->class_().name().c_str();
    if (request->class_().name().length() == 0) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    }

    const auto all_tcs = TrafficClassBuilder::all_tcs();
    auto it = all_tcs.find(tc_name);
    if (it == all_tcs.end()) {
      return return_with_error(response, ENOENT, "Name '%s' doesn't exist",
                               tc_name);
    }

    bess::TrafficClass* c = it->second;

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
    } else {
      return return_with_error(response, EINVAL,
                               "Can only update RateLimit "
                               "TCs");
    }

    return Status::OK;
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

      bess::utils::EthHeader::Address mac_addr;
      memcpy(mac_addr.bytes, p->mac_addr, ETH_ALEN);
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

    bess::utils::EthHeader::Address mac_addr;
    memcpy(mac_addr.bytes, port->mac_addr, ETH_ALEN);
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
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

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

    // TODO(clan): Dump!

    collect_igates(m, response);
    collect_ogates(m, response);
    collect_metadata(m, response);

    return Status::OK;
  }

  Status ConnectModules(ServerContext*, const ConnectModulesRequest* request,
                        EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

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

  Status AttachTask(ServerContext*, const AttachTaskRequest* request,
                    EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    if (!request->name().length()) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    }

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               request->name().c_str());
    }
    Module* m = it->second;

    task_id_t tid = request->taskid();
    if (tid >= MAX_TASKS_PER_MODULE) {
      return return_with_error(response, EINVAL,
                               "'taskid' must be between 0 and %d",
                               MAX_TASKS_PER_MODULE - 1);
    }

    Task* t;
    if (tid >= m->tasks().size() || (t = m->tasks()[tid]) == nullptr) {
      return return_with_error(response, ENOENT, "Task %s:%hu does not exist",
                               request->name().c_str(), tid);
    }

    if (request->identifier_case() == bess::pb::AttachTaskRequest::kTc) {
      const auto& tcs = TrafficClassBuilder::all_tcs();
      const auto& it2 = tcs.find(request->tc());
      if (it2 == tcs.end()) {
        return return_with_error(response, ENOENT, "No TC '%s' found",
                                 request->tc().c_str());
      }
      bess::TrafficClass* c = it2->second;
      if (c->policy() == bess::POLICY_LEAF) {
        t->Attach(static_cast<bess::LeafTrafficClass*>(c));
      } else {
        return return_with_error(response, EINVAL, "TC must be a leaf class");
      }
    } else if (request->identifier_case() ==
               bess::pb::AttachTaskRequest::kWid) {
      int wid = request->wid(); /* TODO: worker_id_t */
      if (wid >= MAX_WORKERS) {
        return return_with_error(response, EINVAL,
                                 "'wid' must be between 0 and %d",
                                 MAX_WORKERS - 1);
      }

      if (!is_worker_active(wid)) {
        return return_with_error(response, EINVAL, "Worker %d does not exist",
                                 wid);
      }

      bess::LeafTrafficClass* tc =
          workers[wid]->scheduler()->default_leaf_class();
      if (!tc) {
        return return_with_error(response, ENOENT,
                                 "Worker %d has no default leaf tc", wid);
      }
      t->Attach(tc);
    } else {
      return return_with_error(response, EINVAL, "Both tc and wid are not set");
    }

    return Status::OK;
  }

  Status EnableTcpdump(ServerContext*, const EnableTcpdumpRequest* request,
                       EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    const char* fifo;
    gate_idx_t gate;
    bool is_igate;

    int ret;

    m_name = request->name().c_str();
    gate = request->gate();
    is_igate = request->is_igate();
    fifo = request->fifo().c_str();

    if (!request->name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    Module* m = it->second;

    if (!is_igate && gate >= m->ogates().size())
      return return_with_error(response, EINVAL,
                               "Output gate '%hu' does not exist", gate);

    if (is_igate && gate >= m->igates().size())
      return return_with_error(response, EINVAL,
                               "Input gate '%hu' does not exist", gate);

    // TODO(melvin): actually change protobufs when new bessctl arrives
    ret = m->EnableTcpDump(fifo, is_igate, gate);

    if (ret < 0) {
      return return_with_error(response, -ret, "Enabling tcpdump %s:%d failed",
                               m_name, gate);
    }

    return Status::OK;
  }

  Status DisableTcpdump(ServerContext*, const DisableTcpdumpRequest* request,
                        EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    gate_idx_t gate;
    bool is_igate;

    int ret;

    m_name = request->name().c_str();
    gate = request->gate();
    is_igate = request->is_igate();

    if (!request->name().length()) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    }

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }

    Module* m = it->second;
    if (!is_igate && gate >= m->ogates().size())
      return return_with_error(response, EINVAL,
                               "Output gate '%hu' does not exist", gate);

    if (is_igate && gate >= m->igates().size())
      return return_with_error(response, EINVAL,
                               "Input gate '%hu' does not exist", gate);

    // TODO(melvin): actually change protobufs when new bessctl arrives
    ret = m->DisableTcpDump(is_igate, gate);

    if (ret < 0) {
      return return_with_error(response, -ret, "Disabling tcpdump %s:%d failed",
                               m_name, gate);
    }
    return Status::OK;
  }

  Status EnableTrack(ServerContext*, const EnableTrackRequest* request,
                     EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    pb_error_t* error = response->mutable_error();
    if (!request->name().length()) {
      for (const auto& it : ModuleBuilder::all_modules()) {
        *error =
            enable_track_for_module(it.second, request->gate(),
                                    request->is_igate(), request->use_gate());
        if (error->err() != 0) {
          return Status::OK;
        }
      }
      return Status::OK;
    } else {
      const auto& it = ModuleBuilder::all_modules().find(request->name());
      if (it == ModuleBuilder::all_modules().end()) {
        *error =
            pb_error(ENOENT, "No module '%s' found", request->name().c_str());
      }
      *error =
          enable_track_for_module(it->second, request->gate(),
                                  request->is_igate(), request->use_gate());
      return Status::OK;
    }
  }

  Status DisableTrack(ServerContext*, const DisableTrackRequest* request,
                      EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    pb_error_t* error = response->mutable_error();
    if (!request->name().length()) {
      for (const auto& it : ModuleBuilder::all_modules()) {
        *error =
            disable_track_for_module(it.second, request->gate(),
                                     request->is_igate(), request->use_gate());
        if (error->err() != 0) {
          return Status::OK;
        }
      }
      return Status::OK;
    } else {
      const auto& it = ModuleBuilder::all_modules().find(request->name());
      if (it == ModuleBuilder::all_modules().end()) {
        *error =
            pb_error(ENOENT, "No module '%s' found", request->name().c_str());
      }
      *error =
          disable_track_for_module(it->second, request->gate(),
                                   request->is_igate(), request->use_gate());
      return Status::OK;
    }
  }

  Status KillBess(ServerContext*, const EmptyRequest*,
                  EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    LOG(WARNING) << "Halt requested by a client\n";
    exit_requested.set_value();

    return Status::OK;
  }

  Status ImportMclass(ServerContext*, const ImportMclassRequest* request,
                      EmptyResponse* response) override {
    VLOG(1) << "Loading module: " << request->path();
    if (!bess::bessd::LoadModule(request->path())) {
      return return_with_error(response, -1, "Failed loading module %s",
                               request->path().c_str());
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

  Status ModuleCommand(ServerContext*, const ModuleCommandRequest* request,
                       ModuleCommandResponse* response) override {
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
};

// TODO: C++-ify
static std::unique_ptr<Server> server;
static BESSControlImpl service;

void SetupControl() {
  ServerBuilder builder;

  if (FLAGS_p) {
    std::string server_address = bess::utils::Format("127.0.0.1:%d", FLAGS_p);
    LOG(INFO) << "Server listening on " << server_address;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  }

  builder.RegisterService(&service);
  server = builder.BuildAndStart();
}

void RunControl() {
  auto serve_func = []() {
    server->Wait();
    LOG(INFO) << "Terminating gRPC server";
  };

  std::thread grpc_server_thread(serve_func);

  auto f = exit_requested.get_future();
  f.wait();

  server->Shutdown();
  grpc_server_thread.join();

  delete server.release();
}
