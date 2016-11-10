#include "bessctl.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>

#include "message.h"
#include "metadata.h"
#include "module.h"
#include "port.h"
#include "service.grpc.pb.h"
#include "tc.h"
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

using namespace bess::pb;

DECLARE_int32(c);
// Capture the port command line flag.
DECLARE_int32(p);

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

static int collect_igates(Module* m, GetModuleInfoResponse* response) {
  for (int i = 0; i < m->igates.curr_size; i++) {
    if (!is_active_gate(&m->igates, i))
      continue;

    GetModuleInfoResponse_IGate* igate = response->add_igates();
    struct gate* g = m->igates.arr[i];
    struct gate* og;

    igate->set_igate(i);

    cdlist_for_each_entry(og, &g->in.ogates_upstream, out.igate_upstream) {
      GetModuleInfoResponse_IGate_OGate* ogate = igate->add_ogates();
      ogate->set_ogate(og->gate_idx);
      ogate->set_name(og->m->name());
    }
  }

  return 0;
}

static int collect_ogates(Module* m, GetModuleInfoResponse* response) {
  for (int i = 0; i < m->ogates.curr_size; i++) {
    if (!is_active_gate(&m->ogates, i))
      continue;
    GetModuleInfoResponse_OGate* ogate = response->add_ogates();
    struct gate* g = m->ogates.arr[i];

    ogate->set_ogate(i);
#if TRACK_GATES
    ogate->set_cnt(g->cnt);
    ogate->set_pkts(g->pkts);
    ogate->set_timestamp(get_epoch_time());
#endif
    ogate->set_name(g->out.igate->m->name());
    ogate->set_igate(g->out.igate->gate_idx);
  }

  return 0;
}

static int collect_metadata(Module* m, GetModuleInfoResponse* response) {
  for (size_t i = 0; i < m->num_attrs; i++) {
    GetModuleInfoResponse_Attribute* attr = response->add_metadata();

    attr->set_name(m->attrs[i].name);
    attr->set_size(m->attrs[i].size);

    switch (m->attrs[i].mode) {
      case bess::metadata::AccessMode::READ:
        attr->set_mode("read");
        break;
      case bess::metadata::AccessMode::WRITE:
        attr->set_mode("write");
        break;
      case bess::metadata::AccessMode::UPDATE:
        attr->set_mode("update");
        break;
      default:
        assert(0);
    }

    attr->set_offset(m->attr_offsets[i]);
  }

  return 0;
}

static ::Port* create_port(const std::string& name, const PortBuilder& driver,
                           queue_t num_inc_q, queue_t num_out_q,
                           size_t size_inc_q, size_t size_out_q,
                           const std::string& mac_addr_str,
                           const google::protobuf::Any& arg, pb_error_t* perr) {
  std::unique_ptr<::Port> p;

  if (num_inc_q == 0)
    num_inc_q = 1;
  if (num_out_q == 0)
    num_out_q = 1;

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
  } else
    mac_addr.Randomize();

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

  if (size_inc_q == 0)
    size_inc_q = p->DefaultIncQueueSize();
  if (size_out_q == 0)
    size_out_q = p->DefaultOutQueueSize();

  memcpy(p->mac_addr, mac_addr.bytes, ETH_ALEN);
  p->num_queues[PACKET_DIR_INC] = num_inc_q;
  p->num_queues[PACKET_DIR_OUT] = num_out_q;
  p->queue_size[PACKET_DIR_INC] = size_inc_q;
  p->queue_size[PACKET_DIR_OUT] = size_out_q;

  *perr = p->Init(arg);
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
  Module* m;
  std::string mod_name;
  if (name.length() > 0) {
    const auto& it = ModuleBuilder::all_modules().find(name);
    if (it != ModuleBuilder::all_modules().end()) {
      *perr = pb_errno(EEXIST);
      return nullptr;
    }
    mod_name = name;
  } else {
    mod_name = ModuleBuilder::GenerateDefaultName(builder.class_name(),
                                                  builder.name_template());
  }

  m = builder.CreateModule(mod_name, &bess::metadata::default_pipeline);

  *perr = m->Init(arg);
  if (perr != nullptr) {
    ModuleBuilder::DestroyModule(m);  // XXX: fix me
    return nullptr;
  }

  if (!builder.AddModule(m)) {
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
      status->set_num_tcs(workers[wid]->s()->num_classes);
      status->set_silent_drops(workers[wid]->silent_drops());
    }
    return Status::OK;
  }
  Status AddWorker(ServerContext*, const AddWorkerRequest* request,
                   EmptyResponse* response) override {
    uint64_t wid = request->wid();
    if (wid >= MAX_WORKERS) {
      return return_with_error(response, EINVAL, "Missing 'wid' field");
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
  Status ResetTcs(ServerContext*, const EmptyRequest*,
                  EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    for (auto it = TCContainer::tcs.begin(); it != TCContainer::tcs.end();) {
      auto it_next = std::next(it);
      struct tc* c = it->second;

      if (c->num_tasks) {
        return return_with_error(response, EBUSY, "TC %s still has %d tasks",
                                 c->settings.name.c_str(), c->num_tasks);
      }

      if (c->settings.auto_free) {
        continue;
      }

      tc_leave(c);
      tc_dec_refcnt(c);

      it = it_next;
    }

    return Status::OK;
  }
  Status ListTcs(ServerContext*, const ListTcsRequest* request,
                 ListTcsResponse* response) override {
    unsigned int wid_filter = MAX_WORKERS;

    wid_filter = request->wid();
    if (wid_filter >= MAX_WORKERS) {
      return return_with_error(
          response, EINVAL, "'wid' must be between 0 and %d", MAX_WORKERS - 1);
    }

    if (!is_worker_active(wid_filter)) {
      return return_with_error(response, EINVAL, "worker:%d does not exist",
                               wid_filter);
    }

    for (const auto& pair : TCContainer::tcs) {
      struct tc* c = pair.second;

      int wid;

      if (wid_filter < MAX_WORKERS) {
        if (workers[wid_filter]->s() != c->s)
          continue;
        wid = wid_filter;
      } else {
        for (wid = 0; wid < MAX_WORKERS; wid++)
          if (is_worker_active(wid) && workers[wid]->s() == c->s)
            break;
      }

      ListTcsResponse_TrafficClassStatus* status =
          response->add_classes_status();

      status->set_parent(c->parent->settings.name);
      status->set_tasks(c->num_tasks);

      status->mutable_class_()->set_name(c->settings.name);
      status->mutable_class_()->set_priority(c->settings.priority);

      if (wid < MAX_WORKERS)
        status->mutable_class_()->set_wid(wid);
      else
        status->mutable_class_()->set_wid(-1);

      status->mutable_class_()->mutable_limit()->set_schedules(
          c->settings.limit[0]);
      status->mutable_class_()->mutable_limit()->set_cycles(
          c->settings.limit[1]);
      status->mutable_class_()->mutable_limit()->set_packets(
          c->settings.limit[2]);
      status->mutable_class_()->mutable_limit()->set_bits(c->settings.limit[3]);

      status->mutable_class_()->mutable_max_burst()->set_schedules(
          c->settings.max_burst[0]);
      status->mutable_class_()->mutable_max_burst()->set_cycles(
          c->settings.max_burst[1]);
      status->mutable_class_()->mutable_max_burst()->set_packets(
          c->settings.max_burst[2]);
      status->mutable_class_()->mutable_max_burst()->set_bits(
          c->settings.max_burst[3]);
    }

    return Status::OK;
  }
  Status AddTc(ServerContext*, const AddTcRequest* request,
               EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    int wid;

    struct tc_params params;
    struct tc* c;

    const char* tc_name = request->class_().name().c_str();
    if (request->class_().name().length() == 0) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    }

    if (TCContainer::tcs.count(tc_name)) {
      return return_with_error(response, EINVAL, "Name '%s' already exists",
                               tc_name);
    }

    wid = request->class_().wid();
    if (wid >= MAX_WORKERS) {
      return return_with_error(
          response, EINVAL, "'wid' must be between 0 and %d", MAX_WORKERS - 1);
    }

    if (!is_worker_active(wid)) {
      if (num_workers == 0 && wid == 0)
        launch_worker(wid, FLAGS_c);
      else {
        return return_with_error(response, EINVAL, "worker:%d does not exist",
                                 wid);
      }
    }

    memset(&params, 0, sizeof(params));
    params.name = tc_name;

    params.priority = request->class_().priority();
    if (params.priority == DEFAULT_PRIORITY)
      return return_with_error(response, EINVAL, "Priority %d is reserved",
                               DEFAULT_PRIORITY);

    /* TODO: add support for other parameters */
    params.share = 1;
    params.share_resource = RESOURCE_CNT;

    if (request->class_().has_limit()) {
      params.limit[0] = request->class_().limit().schedules();
      params.limit[1] = request->class_().limit().cycles();
      params.limit[2] = request->class_().limit().packets();
      params.limit[3] = request->class_().limit().bits();
    }

    if (request->class_().has_max_burst()) {
      params.max_burst[0] = request->class_().max_burst().schedules();
      params.max_burst[1] = request->class_().max_burst().cycles();
      params.max_burst[2] = request->class_().max_burst().packets();
      params.max_burst[3] = request->class_().max_burst().bits();
    }

    c = tc_init(workers[wid]->s(), &params);
    if (is_err(c))
      return return_with_error(response, -ptr_to_err(c), "tc_init() failed");

    tc_join(c);

    return Status::OK;
  }
  Status GetTcStats(ServerContext*, const GetTcStatsRequest* request,
                    GetTcStatsResponse* response) override {
    const char* tc_name = request->name().c_str();

    struct tc* c;

    if (request->name().length() == 0)
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");

    const auto& it = TCContainer::tcs.find(tc_name);
    if (it == TCContainer::tcs.end()) {
      return return_with_error(response, ENOENT, "No TC '%s' found", tc_name);
    }
    c = it->second;

    response->set_timestamp(get_epoch_time());
    response->set_count(c->stats.usage[RESOURCE_CNT]);
    response->set_cycles(c->stats.usage[RESOURCE_CYCLE]);
    response->set_packets(c->stats.usage[RESOURCE_PACKET]);
    response->set_bits(c->stats.usage[RESOURCE_BIT]);

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
      bess::pb::Port* port = response->add_ports();

      port->set_name(p->name());
      port->set_driver(p->port_builder()->class_name());
    }

    return Status::OK;
  }
  Status CreatePort(ServerContext*, const CreatePortRequest* request,
                    CreatePortResponse* response) override {
    const char* driver_name;
    ::Port* port = nullptr;

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
    const char* port_name;
    port_stats_t stats;

    if (!request->name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    port_name = request->name().c_str();

    const auto& it = PortBuilder::all_ports().find(port_name);
    if (it == PortBuilder::all_ports().end()) {
      return return_with_error(response, ENOENT, "No port '%s' found",
                               port_name);
    }
    it->second->GetPortStats(&stats);

    response->mutable_inc()->set_packets(stats[PACKET_DIR_INC].packets);
    response->mutable_inc()->set_dropped(stats[PACKET_DIR_INC].dropped);
    response->mutable_inc()->set_bytes(stats[PACKET_DIR_INC].bytes);

    response->mutable_out()->set_packets(stats[PACKET_DIR_OUT].packets);
    response->mutable_out()->set_dropped(stats[PACKET_DIR_OUT].dropped);
    response->mutable_out()->set_bytes(stats[PACKET_DIR_OUT].bytes);

    response->set_timestamp(get_epoch_time());

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
    int cnt = 1;
    int offset;

    for (offset = 0; cnt != 0; offset += cnt) {
      ListModulesResponse_Module* module = response->add_modules();

      for (const auto& pair : ModuleBuilder::all_modules()) {
        const Module* m = pair.second;

        module->set_name(m->name());
        module->set_mclass(m->module_builder()->class_name());
        module->set_desc(m->GetDesc());
      }
    };

    return Status::OK;
  }
  Status CreateModule(ServerContext*, const CreateModuleRequest* request,
                      CreateModuleResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* mclass_name;
    Module* module;

    if (!request->mclass().length())
      return return_with_error(response, EINVAL, "Missing 'mclass' field");
    mclass_name = request->mclass().c_str();

    const auto& builders = ModuleBuilder::all_module_builders();
    const auto& it = builders.find(mclass_name);
    if (it == builders.end()) {
      return return_with_error(response, ENOENT, "No mclass '%s' found",
                               mclass_name);
    }
    const ModuleBuilder& builder = it->second;

    pb_error_t* error = response->mutable_error();
    module = create_module(request->name(), builder, request->arg(), error);

    if (!module)
      return Status::OK;

    response->set_name(module->name());
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

    // TODO: Dump!

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
    const char* m_name;
    const char* tc_name;

    task_id_t tid;

    Module* m;
    struct task* t;

    m_name = request->name().c_str();

    if (!request->name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    m = it->second;

    tid = request->taskid();
    if (tid >= MAX_TASKS_PER_MODULE)
      return return_with_error(response, EINVAL,
                               "'taskid' must be between 0 and %d",
                               MAX_TASKS_PER_MODULE - 1);

    if ((t = m->tasks[tid]) == nullptr)
      return return_with_error(response, ENOENT, "Task %s:%hu does not exist",
                               m_name, tid);

    tc_name = request->tc().c_str();

    if (request->tc().length() > 0) {
      struct tc* c;

      const auto& it2 = TCContainer::tcs.find(tc_name);
      if (it2 == TCContainer::tcs.end()) {
        return return_with_error(response, ENOENT, "No TC '%s' found", tc_name);
      }
      c = it2->second;

      task_attach(t, c);
    } else {
      int wid; /* TODO: worker_id_t */

      if (task_is_attached(t))
        return return_with_error(response, EBUSY,
                                 "Task %s:%hu is already "
                                 "attached to a TC",
                                 m_name, tid);

      wid = request->wid();
      if (wid >= MAX_WORKERS)
        return return_with_error(response, EINVAL,
                                 "'wid' must be between 0 and %d",
                                 MAX_WORKERS - 1);

      if (!is_worker_active(wid))
        return return_with_error(response, EINVAL, "Worker %d does not exist",
                                 wid);

      assign_default_tc(wid, t);
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
    gate_idx_t ogate;

    int ret;

    m_name = request->name().c_str();
    ogate = request->ogate();
    fifo = request->fifo().c_str();

    if (!request->name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }
    Module* m = it->second;

    if (ogate >= m->ogates.curr_size)
      return return_with_error(response, EINVAL,
                               "Output gate '%hu' does not exist", ogate);

    ret = m->EnableTcpDump(fifo, ogate);

    if (ret < 0) {
      return return_with_error(response, -ret, "Enabling tcpdump %s:%d failed",
                               m_name, ogate);
    }

    return Status::OK;
  }
  Status DisableTcpdump(ServerContext*, const DisableTcpdumpRequest* request,
                        EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    gate_idx_t ogate;

    int ret;

    m_name = request->name().c_str();
    ogate = request->ogate();

    if (!request->name().length()) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    }

    const auto& it = ModuleBuilder::all_modules().find(request->name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);
    }

    Module* m = it->second;
    if (ogate >= m->ogates.curr_size) {
      return return_with_error(response, EINVAL,
                               "Output gate '%hu' does not exist", ogate);
    }

    ret = m->DisableTcpDump(ogate);

    if (ret < 0) {
      return return_with_error(response, -ret, "Disabling tcpdump %s:%d failed",
                               m_name, ogate);
    }
    return Status::OK;
  }

  Status KillBess(ServerContext*, const EmptyRequest*,
                  EmptyResponse* response) override {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    LOG(WARNING) << "Halt requested by a client\n";
    exit(EXIT_SUCCESS);

    /* Never called */
    return Status::OK;
  }

  Status ListMclass(ServerContext*, const EmptyRequest*,
                    ListMclassResponse* response) override {
    for (const auto& pair : ModuleBuilder::all_module_builders()) {
      const ModuleBuilder& builder = pair.second;
      response->add_name(builder.class_name());
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
    for (const std::string& cmd : cls->cmds()) {
      response->add_cmds(cmd);
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
    Module* m = it->second;
    *response = m->RunCommand(request->cmd(), request->arg());
    return Status::OK;
  }
};

static void reset_core_affinity() {
  cpu_set_t set;
  unsigned int i;

  CPU_ZERO(&set);

  /* set all cores... */
  for (i = 0; i < rte_lcore_count(); i++)
    CPU_SET(i, &set);

  /* ...and then unset the ones where workers run */
  for (i = 0; i < MAX_WORKERS; i++)
    if (is_worker_active(i))
      CPU_CLR(workers[i]->core(), &set);

  rte_thread_set_affinity(&set);
}

void SetupControl() {
  reset_core_affinity();
  ctx.SetNonWorker();
}

void RunControl() {
  BESSControlImpl service;
  std::string server_address;
  ServerBuilder builder;

  if (FLAGS_p) {
    server_address = bess::utils::Format("127.0.0.1:%d", FLAGS_p);
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  }

  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}
