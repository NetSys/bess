#include <gflags/gflags.h>

#include <rte_config.h>
#include <rte_ether.h>

#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>

#include "service.grpc.pb.h"

#include "bessctl.h"
#include "message.h"
#include "module.h"
#include "port.h"
#include "tc.h"
#include "utils/time.h"
#include "worker.h"

#include "modules/bpf.h"
#include "modules/dump.h"
#include "modules/exact_match.h"
#include "modules/hash_lb.h"
#include "modules/ip_lookup.h"
#include "modules/l2_forward.h"
#include "modules/measure.h"
#include "modules/port_inc.h"
#include "modules/queue.h"
#include "modules/queue_inc.h"
#include "modules/random_update.h"
#include "modules/rewrite.h"
#include "modules/round_robin.h"
#include "modules/source.h"
#include "modules/update.h"
#include "modules/vlan_push.h"
#include "modules/wildcard_match.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;
using grpc::ClientContext;
using grpc::ServerBuilder;

using namespace bess::protobuf;

DECLARE_int32(c);
// Capture the port command line flag.
DECLARE_int32(p);

template <typename T>
static inline Status return_with_error(T* response, int code, const char* fmt,
                                       ...) {
  va_list ap;
  va_start(ap, fmt);
  response->mutable_error()->set_err(code);
  response->mutable_error()->set_errmsg(string_vformat(fmt, ap));
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

template <typename T>
static ::Port* create_port(const std::string& name, const PortBuilder& driver,
                           queue_t num_inc_q, queue_t num_out_q,
                           size_t size_inc_q, size_t size_out_q,
                           const std::string& mac_addr_str, const T& arg,
                           pb_error_t* perr) {
  std::unique_ptr<::Port> p;
  int ret;

  if (num_inc_q == 0)
    num_inc_q = 1;
  if (num_out_q == 0)
    num_out_q = 1;

  uint8_t mac_addr[ETH_ALEN];

  if (mac_addr_str.length() > 0) {
    ret = sscanf(mac_addr_str.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
                 &mac_addr[0], &mac_addr[1], &mac_addr[2], &mac_addr[3],
                 &mac_addr[4], &mac_addr[5]);

    if (ret != 6) {
      perr->set_err(EINVAL);
      perr->set_errmsg(
          "MAC address should be "
          "formatted as a string "
          "xx:xx:xx:xx:xx:xx");
      return nullptr;
    }
  } else
    eth_random_addr(mac_addr);

  if (num_inc_q > MAX_QUEUES_PER_DIR || num_out_q > MAX_QUEUES_PER_DIR) {
    perr->set_err(EINVAL);
    perr->set_errmsg("Invalid number of queues");
    return nullptr;
  }

  if (size_inc_q < 0 || size_inc_q > MAX_QUEUE_SIZE || size_out_q < 0 ||
      size_out_q > MAX_QUEUE_SIZE) {
    perr->set_err(EINVAL);
    perr->set_errmsg("Invalid queue size");
    return nullptr;
  }

  std::string port_name;

  if (name.length() > 0) {
    if (PortBuilder::all_ports().count(name)) {
      perr->set_err(EEXIST);
      perr->set_errmsg(string_format("Port '%s' already exists", name.c_str()));
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

  memcpy(p->mac_addr, mac_addr, ETH_ALEN);
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

template <typename T>
static Module* create_module(const char* name, const ModuleBuilder& builder,
                             const T& arg, pb_error_t* perr) {
  Module* m;
  std::string mod_name;
  if (name) {
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

  m = builder.CreateModule(mod_name, &default_pipeline);

  *perr = m->Init(&arg);
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
  Status ResetAll(ClientContext* context, const Empty& request,
                  EmptyResponse* response) {
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

  Status PauseAll(ClientContext*, const Empty&, EmptyResponse*) {
    pause_all_workers();
    log_info("*** All workers have been paused ***\n");
    return Status::OK;
  }
 
  Status ResumeAll(ClientContext*, const Empty&, EmptyResponse*) {
    log_info("*** Resuming ***\n");
    resume_all_workers();
    return Status::OK;
  }
  Status ResetWorkers(ClientContext*, const Empty&,
                      EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    destroy_all_workers();
    log_info("*** All workers have been destroyed ***\n");
    return Status::OK;
  }
  Status ListWorkers(ClientContext*, const Empty&,
                     ListWorkersResponse* response) {
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
  Status AddWorker(ClientContext*, const AddWorkerRequest& request,
                   EmptyResponse* response) {
    uint64_t wid = request.wid();
    if (wid >= MAX_WORKERS) {
      return return_with_error(response, EINVAL, "Missing 'wid' field");
    }
    uint64_t core = request.core();
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
  Status ResetTcs(ClientContext*, const Empty&,
                  EmptyResponse* response) {
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
  Status ListTcs(ClientContext*, const ListTcsRequest& request,
                 ListTcsResponse* response) {
    unsigned int wid_filter = MAX_WORKERS;

    wid_filter = request.wid();
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
  Status AddTc(ClientContext*, const AddTcRequest& request,
               EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    int wid;

    struct tc_params params;
    struct tc* c;

    const char* tc_name = request.class_().name().c_str();
    if (request.class_().name().length() == 0) {
      return return_with_error(response, EINVAL, "Missing 'name' field");
    }

    if (TCContainer::tcs.count(tc_name)) {
      return return_with_error(response, EINVAL, "Name '%s' already exists",
                               tc_name);
    }

    wid = request.class_().wid();
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

    params.priority = request.class_().priority();
    if (params.priority == DEFAULT_PRIORITY)
      return return_with_error(response, EINVAL, "Priority %d is reserved",
                               DEFAULT_PRIORITY);

    /* TODO: add support for other parameters */
    params.share = 1;
    params.share_resource = RESOURCE_CNT;

    if (request.class_().has_limit()) {
      params.limit[0] = request.class_().limit().schedules();
      params.limit[1] = request.class_().limit().cycles();
      params.limit[2] = request.class_().limit().packets();
      params.limit[3] = request.class_().limit().bits();
    }

    if (request.class_().has_max_burst()) {
      params.max_burst[0] = request.class_().max_burst().schedules();
      params.max_burst[1] = request.class_().max_burst().cycles();
      params.max_burst[2] = request.class_().max_burst().packets();
      params.max_burst[3] = request.class_().max_burst().bits();
    }

    c = tc_init(workers[wid]->s(), &params);
    if (is_err(c))
      return return_with_error(response, -ptr_to_err(c), "tc_init() failed");

    tc_join(c);

    return Status::OK;
  }
  Status GetTcStats(ClientContext*, const GetTcStatsRequest& request,
                    GetTcStatsResponse* response) {
    const char* tc_name = request.name().c_str();

    struct tc* c;

    if (request.name().length() == 0)
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
  Status ListDrivers(ClientContext*, const Empty&,
                     ListDriversResponse* response) {
    for (const auto& pair : PortBuilder::all_port_builders()) {
      const PortBuilder& builder = pair.second;
      response->add_driver_names(builder.class_name());
    }

    return Status::OK;
  }
  Status GetDriverInfo(ClientContext*, const GetDriverInfoRequest& request,
                       GetDriverInfoResponse* response) {
    if (request.driver_name().length() == 0) {
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    }

    const auto& it =
        PortBuilder::all_port_builders().find(request.driver_name());
    if (it == PortBuilder::all_port_builders().end()) {
      return return_with_error(response, ENOENT, "No driver '%s' found",
                               request.driver_name().c_str());
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
  Status ResetPorts(ClientContext*, const Empty&,
                    EmptyResponse* response) {
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

    log_info("*** All ports have been destroyed ***\n");
    return Status::OK;
  }
  Status ListPorts(ClientContext*, const Empty&,
                   ListPortsResponse* response) {
    for (const auto& pair : PortBuilder::all_ports()) {
      const ::Port* p = pair.second;
      bess::protobuf::Port* port = response->add_ports();

      port->set_name(p->name());
      port->set_driver(p->port_builder()->class_name());
    }

    return Status::OK;
  }
  Status CreatePort(ClientContext*, const CreatePortRequest& request,
                    CreatePortResponse* response) {
    const char* driver_name;
    ::Port* port;

    if (request.port().driver().length() == 0)
      return return_with_error(response, EINVAL, "Missing 'driver' field");

    driver_name = request.port().driver().c_str();
    const auto& builders = PortBuilder::all_port_builders();
    const auto& it = builders.find(driver_name);
    if (it == builders.end()) {
      return return_with_error(response, ENOENT, "No port driver '%s' found",
                               driver_name);
    }

    const PortBuilder& builder = it->second;
    pb_error_t* error = response->mutable_error();

    switch (request.arg_case()) {
      case CreatePortRequest::kPcapArg:
        port = create_port(request.port().name(), builder, request.num_inc_q(),
                           request.num_out_q(), request.size_inc_q(),
                           request.size_out_q(), request.mac_addr(),
                           request.pcap_arg(), error);
        break;
      case CreatePortRequest::kPmdArg:
        port = create_port(request.port().name(), builder, request.num_inc_q(),
                           request.num_out_q(), request.size_inc_q(),
                           request.size_out_q(), request.mac_addr(),
                           request.pmd_arg(), error);
        break;
      case CreatePortRequest::kSocketArg:
        port = create_port(request.port().name(), builder, request.num_inc_q(),
                           request.num_out_q(), request.size_inc_q(),
                           request.size_out_q(), request.mac_addr(),
                           request.socket_arg(), error);
        break;
      case CreatePortRequest::kZcvportArg:
        port = create_port(request.port().name(), builder, request.num_inc_q(),
                           request.num_out_q(), request.size_inc_q(),
                           request.size_out_q(), request.mac_addr(),
                           request.zcvport_arg(), error);
        break;
      case CreatePortRequest::kVportArg:
        port = create_port(request.port().name(), builder, request.num_inc_q(),
                           request.num_out_q(), request.size_inc_q(),
                           request.size_out_q(), request.mac_addr(),
                           request.vport_arg(), error);
        break;
      case CreatePortRequest::ARG_NOT_SET:
        return return_with_error(response, CreatePortRequest::ARG_NOT_SET,
                                 "Missing argument");
    }

    if (!port)
      return Status::OK;

    response->set_name(port->name());
    return Status::OK;
  }
  Status DestroyPort(ClientContext*, const DestroyPortRequest& request,
                     EmptyResponse* response) {
    const char* port_name;
    int ret;

    if (!request.name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");

    port_name = request.name().c_str();
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
  Status GetPortStats(ClientContext*, const GetPortStatsRequest& request,
                      GetPortStatsResponse* response) {
    const char* port_name;
    port_stats_t stats;

    if (!request.name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    port_name = request.name().c_str();

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
  Status ResetModules(ClientContext*, const Empty&,
                      EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }

    ModuleBuilder::DestroyAllModules();
    log_info("*** All modules have been destroyed ***\n");
    return Status::OK;
  }
  Status ListModules(ClientContext*, const Empty&,
                     ListModulesResponse* response) {
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
  Status CreateModule(ClientContext*, const CreateModuleRequest& request,
                      CreateModuleResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* mclass_name;
    Module* module;

    if (!request.mclass().length())
      return return_with_error(response, EINVAL, "Missing 'mclass' field");
    mclass_name = request.mclass().c_str();

    const auto& builders = ModuleBuilder::all_module_builders();
    const auto& it = builders.find(mclass_name);
    if (it == builders.end()) {
      return return_with_error(response, ENOENT, "No mclass '%s' found",
                               mclass_name);
    }
    const ModuleBuilder& builder = it->second;

    pb_error_t* error = response->mutable_error();

    switch (request.arg_case()) {
      case CreateModuleRequest::kBpfArg:
        module = create_module(request.name().c_str(), builder,
                               request.bpf_arg(), error);
        break;
      case CreateModuleRequest::kBufferArg:
        module = create_module(request.name().c_str(), builder,
                               request.buffer_arg(), error);
        break;
      case CreateModuleRequest::kBypassArg:
        module = create_module(request.name().c_str(), builder,
                               request.bypass_arg(), error);
        break;
      case CreateModuleRequest::kDumpArg:
        module = create_module(request.name().c_str(), builder,
                               request.dump_arg(), error);
        break;
      case CreateModuleRequest::kEtherEncapArg:
        module = create_module(request.name().c_str(), builder,
                               request.ether_encap_arg(), error);
        break;
      case CreateModuleRequest::kExactMatchArg:
        module = create_module(request.name().c_str(), builder,
                               request.exact_match_arg(), error);
        break;
      case CreateModuleRequest::kFlowGenArg:
        module = create_module(request.name().c_str(), builder,
                               request.flow_gen_arg(), error);
        break;
      case CreateModuleRequest::kGenericDecapArg:
        module = create_module(request.name().c_str(), builder,
                               request.generic_decap_arg(), error);
        break;
      case CreateModuleRequest::kGenericEncapArg:
        module = create_module(request.name().c_str(), builder,
                               request.generic_encap_arg(), error);
        break;
      case CreateModuleRequest::kHashLbArg:
        module = create_module(request.name().c_str(), builder,
                               request.hash_lb_arg(), error);
        break;
      case CreateModuleRequest::kIpEncapArg:
        module = create_module(request.name().c_str(), builder,
                               request.ip_encap_arg(), error);
        break;
      case CreateModuleRequest::kIpLookupArg:
        module = create_module(request.name().c_str(), builder,
                               request.ip_lookup_arg(), error);
        break;
      case CreateModuleRequest::kL2ForwardArg:
        module = create_module(request.name().c_str(), builder,
                               request.l2_forward_arg(), error);
        break;
      case CreateModuleRequest::kMacSwapArg:
        module = create_module(request.name().c_str(), builder,
                               request.mac_swap_arg(), error);
        break;
      case CreateModuleRequest::kMeasureArg:
        module = create_module(request.name().c_str(), builder,
                               request.measure_arg(), error);
        break;
      case CreateModuleRequest::kMergeArg:
        module = create_module(request.name().c_str(), builder,
                               request.merge_arg(), error);
        break;
      case CreateModuleRequest::kMetadataTestArg:
        module = create_module(request.name().c_str(), builder,
                               request.metadata_test_arg(), error);
        break;
      case CreateModuleRequest::kNoopArg:
        module = create_module(request.name().c_str(), builder,
                               request.noop_arg(), error);
        break;
      case CreateModuleRequest::kPortIncArg:
        module = create_module(request.name().c_str(), builder,
                               request.port_inc_arg(), error);
        break;
      case CreateModuleRequest::kPortOutArg:
        module = create_module(request.name().c_str(), builder,
                               request.port_out_arg(), error);
        break;
      case CreateModuleRequest::kQueueIncArg:
        module = create_module(request.name().c_str(), builder,
                               request.queue_inc_arg(), error);
        break;
      case CreateModuleRequest::kQueueOutArg:
        module = create_module(request.name().c_str(), builder,
                               request.queue_out_arg(), error);
        break;
      case CreateModuleRequest::kQueueArg:
        module = create_module(request.name().c_str(), builder,
                               request.queue_arg(), error);
        break;
      case CreateModuleRequest::kRandomUpdateArg:
        module = create_module(request.name().c_str(), builder,
                               request.random_update_arg(), error);
        break;
      case CreateModuleRequest::kRewriteArg:
        module = create_module(request.name().c_str(), builder,
                               request.rewrite_arg(), error);
        break;
      case CreateModuleRequest::kRoundRobinArg:
        module = create_module(request.name().c_str(), builder,
                               request.round_robin_arg(), error);
        break;
      case CreateModuleRequest::kSetMetadataArg:
        module = create_module(request.name().c_str(), builder,
                               request.set_metadata_arg(), error);
        break;
      case CreateModuleRequest::kSinkArg:
        module = create_module(request.name().c_str(), builder,
                               request.sink_arg(), error);
        break;
      case CreateModuleRequest::kSourceArg:
        module = create_module(request.name().c_str(), builder,
                               request.source_arg(), error);
        break;
      case CreateModuleRequest::kSplitArg:
        module = create_module(request.name().c_str(), builder,
                               request.split_arg(), error);
        break;
      case CreateModuleRequest::kTimestampArg:
        module = create_module(request.name().c_str(), builder,
                               request.timestamp_arg(), error);
        break;
      case CreateModuleRequest::kUpdateArg:
        module = create_module(request.name().c_str(), builder,
                               request.update_arg(), error);
        break;
      case CreateModuleRequest::kVlanPopArg:
        module = create_module(request.name().c_str(), builder,
                               request.vlan_pop_arg(), error);
        break;
      case CreateModuleRequest::kVlanPushArg:
        module = create_module(request.name().c_str(), builder,
                               request.vlan_push_arg(), error);
        break;
      case CreateModuleRequest::kVlanSplitArg:
        module = create_module(request.name().c_str(), builder,
                               request.vlan_split_arg(), error);
        break;
      case CreateModuleRequest::kVxlanEncapArg:
        module = create_module(request.name().c_str(), builder,
                               request.vxlan_encap_arg(), error);
        break;
      case CreateModuleRequest::kVxlanDecapArg:
        module = create_module(request.name().c_str(), builder,
                               request.vxlan_decap_arg(), error);
        break;
      case CreateModuleRequest::kWildcardMatchArg:
        module = create_module(request.name().c_str(), builder,
                               request.wildcard_match_arg(), error);
        break;
      case CreateModuleRequest::ARG_NOT_SET:
      default:
        return return_with_error(response, CreateModuleRequest::ARG_NOT_SET,
                                 "Missing argument");
    }

    if (!module)
      return Status::OK;

    response->set_name(module->name());
    return Status::OK;
  }
  Status DestroyModule(ClientContext*, const DestroyModuleRequest& request,
                       EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    Module* m;

    if (!request.name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    m_name = request.name().c_str();

    if (!ModuleBuilder::all_modules().count(m_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);

    ModuleBuilder::DestroyModule(m);

    return Status::OK;
  }
  Status GetModuleInfo(ClientContext*, const GetModuleInfoRequest& request,
                       GetModuleInfoResponse* response) {
    const char* m_name;
    Module* m;

    if (!request.name().length())
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    m_name = request.name().c_str();

    if (!ModuleBuilder::all_modules().count(m_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);

    response->set_name(m->name());
    response->set_mclass(m->module_builder()->class_name());

    response->set_desc(m->GetDesc());

    // TODO: Dump!

    collect_igates(m, response);
    collect_ogates(m, response);
    collect_metadata(m, response);

    return Status::OK;
  }
  Status ConnectModules(ClientContext*, const ConnectModulesRequest& request,
                        EmptyResponse* response) {
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

    m1_name = request.m1().c_str();
    m2_name = request.m2().c_str();
    ogate = request.ogate();
    igate = request.igate();

    if (!m1_name || !m2_name)
      return return_with_error(response, EINVAL, "Missing 'm1' or 'm2' field");

    if (!ModuleBuilder::all_modules().count(m1_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m1_name);

    if (!ModuleBuilder::all_modules().count(m2_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m2_name);

    ret = m1->ConnectModules(ogate, m2, igate);
    if (ret < 0)
      return return_with_error(response, -ret, "Connection %s:%d->%d:%s failed",
                               m1_name, ogate, igate, m2_name);

    return Status::OK;
  }
  Status DisconnectModules(ClientContext*,
                           const DisconnectModulesRequest& request,
                           EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    gate_idx_t ogate;

    Module* m;

    int ret;

    m_name = request.name().c_str();
    ogate = request.ogate();

    if (!request.name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    if (!ModuleBuilder::all_modules().count(m_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);

    ret = m->DisconnectModules(ogate);
    if (ret < 0)
      return return_with_error(response, -ret, "Disconnection %s:%d failed",
                               m_name, ogate);

    return Status::OK;
  }
  Status AttachTask(ClientContext*, const AttachTaskRequest& request,
                    EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    const char* tc_name;

    task_id_t tid;

    Module* m;
    struct task* t;

    m_name = request.name().c_str();

    if (!request.name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    if (!ModuleBuilder::all_modules().count(m_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);

    tid = request.taskid();
    if (tid >= MAX_TASKS_PER_MODULE)
      return return_with_error(response, EINVAL,
                               "'taskid' must be between 0 and %d",
                               MAX_TASKS_PER_MODULE - 1);

    if ((t = m->tasks[tid]) == nullptr)
      return return_with_error(response, ENOENT, "Task %s:%hu does not exist",
                               m_name, tid);

    tc_name = request.tc().c_str();

    if (request.tc().length() > 0) {
      struct tc* c;

      const auto& it = TCContainer::tcs.find(tc_name);
      if (it == TCContainer::tcs.end()) {
        return return_with_error(response, ENOENT, "No TC '%s' found", tc_name);
      }
      c = it->second;

      task_attach(t, c);
    } else {
      int wid; /* TODO: worker_id_t */

      if (task_is_attached(t))
        return return_with_error(response, EBUSY,
                                 "Task %s:%hu is already "
                                 "attached to a TC",
                                 m_name, tid);

      wid = request.wid();
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
  Status EnableTcpdump(ClientContext*, const EnableTcpdumpRequest& request,
                       EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    const char* fifo;
    gate_idx_t ogate;

    Module* m;

    int ret;

    m_name = request.name().c_str();
    ogate = request.ogate();
    fifo = request.fifo().c_str();

    if (!request.name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    if (!ModuleBuilder::all_modules().count(m_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);

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
  Status DisableTcpdump(ClientContext*, const DisableTcpdumpRequest& request,
                        EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    const char* m_name;
    gate_idx_t ogate;

    Module* m;

    int ret;

    m_name = request.name().c_str();
    ogate = request.ogate();

    if (!request.name().length())
      return return_with_error(response, EINVAL, "Missing 'name' field");

    if (!ModuleBuilder::all_modules().count(m_name))
      return return_with_error(response, ENOENT, "No module '%s' found",
                               m_name);

    if (ogate >= m->ogates.curr_size)
      return return_with_error(response, EINVAL,
                               "Output gate '%hu' does not exist", ogate);

    ret = m->DisableTcpDump(ogate);

    if (ret < 0) {
      return return_with_error(response, -ret, "Disabling tcpdump %s:%d failed",
                               m_name, ogate);
    }
    return Status::OK;
  }

  Status KillBess(ClientContext*, const Empty&,
                  EmptyResponse* response) {
    if (is_any_worker_running()) {
      return return_with_error(response, EBUSY, "There is a running worker");
    }
    log_notice("Halt requested by a client\n");
    exit(EXIT_SUCCESS);

    /* Never called */
    return Status::OK;
  }

  Status ListMclass(ClientContext*, const Empty&,
                    ListMclassResponse* response) {
    for (const auto& pair : ModuleBuilder::all_module_builders()) {
      const ModuleBuilder& builder = pair.second;
      response->add_name(builder.class_name());
    }
    return Status::OK;
  }

  Status GetMclassInfo(ClientContext*, const GetMclassInfoRequest& request,
                       GetMclassInfoResponse* response) {
    if (!request.name().length()) {
      return return_with_error(response, EINVAL,
                               "Argument must be a name in str");
    }

    const std::string& cls_name = request.name();
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

  Status ModuleCommand(ClientContext*, const ModuleCommandRequest& request,
                       ModuleCommandResponse* response) {
    if (!request.name().length()) {
      return return_with_error(response->mutable_empty(), EINVAL,
                               "Missing module name field 'name'");
    }
    const auto& it = ModuleBuilder::all_modules().find(request.name());
    if (it == ModuleBuilder::all_modules().end()) {
      return return_with_error(response->mutable_empty(), ENOENT,
                               "No module '%s' found", request.name().c_str());
    }
    Module* m = it->second;
    MeasureCommandGetSummaryResponse* summary;
    L2ForwardCommandLookupResponse* lookup_result;

    pb_error_t* error = response->mutable_empty()->mutable_error();

    switch (request.cmd_case()) {
      case ModuleCommandRequest::kBpfAddArg:
        *error = reinterpret_cast<BPF*>(m)->CommandAdd(request.bpf_add_arg());
        break;
      case ModuleCommandRequest::kBpfClearArg:
        *error =
            reinterpret_cast<BPF*>(m)->CommandClear(request.bpf_clear_arg());
        break;
      case ModuleCommandRequest::kDumpSetIntervalArg:
        *error = reinterpret_cast<Dump*>(m)
                     ->CommandSetInterval(request.dump_set_interval_arg());
        break;
      case ModuleCommandRequest::kExactmatchAddArg:
        *error = reinterpret_cast<ExactMatch*>(m)
                     ->CommandAdd(request.exactmatch_add_arg());
        break;
      case ModuleCommandRequest::kExactmatchDeleteArg:
        *error = reinterpret_cast<ExactMatch*>(m)
                     ->CommandDelete(request.exactmatch_delete_arg());
        break;
      case ModuleCommandRequest::kExactmatchClearArg:
        *error = reinterpret_cast<ExactMatch*>(m)->CommandSetDefaultGate(
            request.exactmatch_set_default_gate_arg());
        break;
      case ModuleCommandRequest::kHashlbSetModeArg:
        *error = reinterpret_cast<bess::modules::HashLB*>(m)
                     ->CommandSetMode(request.hashlb_set_mode_arg());
        break;
      case ModuleCommandRequest::kHashlbSetGatesArg:
        *error = reinterpret_cast<bess::modules::HashLB*>(m)
                     ->CommandSetGates(request.hashlb_set_gates_arg());
        break;
      case ModuleCommandRequest::kIplookupAddArg:
        *error = reinterpret_cast<IPLookup*>(m)
                     ->CommandAdd(request.iplookup_add_arg());
        break;
      case ModuleCommandRequest::kIplookupClearArg:
        *error = reinterpret_cast<IPLookup*>(m)
                     ->CommandClear(request.iplookup_clear_arg());
        break;
      case ModuleCommandRequest::kL2ForwardAddArg:
        *error = reinterpret_cast<L2Forward*>(m)
                     ->CommandAdd(request.l2forward_add_arg());
        break;
      case ModuleCommandRequest::kL2ForwardDeleteArg:
        *error = reinterpret_cast<L2Forward*>(m)
                     ->CommandDelete(request.l2forward_delete_arg());
        break;
      case ModuleCommandRequest::kL2ForwardSetDefaultGateArg:
        *error = reinterpret_cast<L2Forward*>(m)->CommandSetDefaultGate(
            request.l2forward_set_default_gate_arg());
        break;
      case ModuleCommandRequest::kL2ForwardLookupArg:
        lookup_result = response->mutable_l2forward_lookup();
        *lookup_result = reinterpret_cast<L2Forward*>(m)
                             ->CommandLookup(request.l2forward_lookup_arg());
        break;
      case ModuleCommandRequest::kL2ForwardPopulateArg:
        *error = reinterpret_cast<L2Forward*>(m)
                     ->CommandPopulate(request.l2forward_populate_arg());
        break;
      case ModuleCommandRequest::kMeasureGetSummaryArg:
        summary = response->mutable_measure_summary();
        *summary = reinterpret_cast<Measure*>(m)
                       ->CommandGetSummary(request.measure_get_summary_arg());
        break;
      case ModuleCommandRequest::kPortincSetBurstArg:
        *error = reinterpret_cast<PortInc*>(m)
                     ->CommandSetBurst(request.portinc_set_burst_arg());
        break;
      case ModuleCommandRequest::kQueueincSetBurstArg:
        *error = reinterpret_cast<QueueInc*>(m)
                     ->CommandSetBurst(request.queueinc_set_burst_arg());
        break;
      case ModuleCommandRequest::kQueueSetSizeArg:
        *error = reinterpret_cast<Queue*>(m)
                     ->CommandSetSize(request.queue_set_size_arg());
        break;
      case ModuleCommandRequest::kQueueSetBurstArg:
        *error = reinterpret_cast<Queue*>(m)
                     ->CommandSetBurst(request.queue_set_burst_arg());
        break;
      case ModuleCommandRequest::kRandomUpdateAddArg:
        *error = reinterpret_cast<RandomUpdate*>(m)
                     ->CommandAdd(request.random_update_add_arg());
        break;
      case ModuleCommandRequest::kRandomUpdateClearArg:
        *error = reinterpret_cast<RandomUpdate*>(m)
                     ->CommandClear(request.random_update_clear_arg());
        break;
      case ModuleCommandRequest::kRewriteAddArg:
        *error = reinterpret_cast<Rewrite*>(m)
                     ->CommandAdd(request.rewrite_add_arg());
        break;
      case ModuleCommandRequest::kRewriteClearArg:
        *error = reinterpret_cast<Rewrite*>(m)
                     ->CommandClear(request.rewrite_clear_arg());
        break;
      case ModuleCommandRequest::kRoundrobinSetGatesArg:
        *error = reinterpret_cast<RoundRobin*>(m)
                     ->CommandSetGates(request.roundrobin_set_gates_arg());
        break;
      case ModuleCommandRequest::kRoundrobinSetModeArg:
        *error = reinterpret_cast<RoundRobin*>(m)
                     ->CommandSetMode(request.roundrobin_set_mode_arg());
        break;
      case ModuleCommandRequest::kSourceSetBurstArg:
        *error = reinterpret_cast<Source*>(m)
                     ->CommandSetBurst(request.source_set_burst_arg());
        break;
      case ModuleCommandRequest::kSourceSetPktSizeArg:
        *error = reinterpret_cast<Source*>(m)
                     ->CommandSetPktSize(request.source_set_pkt_size_arg());
        break;
      case ModuleCommandRequest::kUpdateAddArg:
        *error =
            reinterpret_cast<Update*>(m)->CommandAdd(request.update_add_arg());
        break;
      case ModuleCommandRequest::kUpdateClearArg:
        *error = reinterpret_cast<Update*>(m)
                     ->CommandClear(request.update_clear_arg());
        break;
      case ModuleCommandRequest::kVlanSetTciArg:
        *error = reinterpret_cast<VLANPush*>(m)
                     ->CommandSetTci(request.vlan_set_tci_arg());
        break;
      case ModuleCommandRequest::kWildcardAddArg:
        *error = reinterpret_cast<WildcardMatch*>(m)
                     ->CommandAdd(request.wildcard_add_arg());
        break;
      case ModuleCommandRequest::kWildcardDeleteArg:
        *error = reinterpret_cast<WildcardMatch*>(m)
                     ->CommandDelete(request.wildcard_delete_arg());
        break;
      case ModuleCommandRequest::kWildcardClearArg:
        *error = reinterpret_cast<WildcardMatch*>(m)
                     ->CommandClear(request.wildcard_clear_arg());
        break;
      case ModuleCommandRequest::kWildcardSetDefaultGateArg:
        *error = reinterpret_cast<WildcardMatch*>(m)->CommandSetDefaultGate(
            request.wildcard_set_default_gate_arg());
        break;
      case ModuleCommandRequest::CMD_NOT_SET:
      default:
        return return_with_error(response->mutable_empty(),
                                 ModuleCommandRequest::CMD_NOT_SET,
                                 "Missing cmd argument");
    }
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
    server_address = string_format("127.0.0.1:%d", FLAGS_p);
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  }

  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}
