#include "snctl.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glog/logging.h>

#include "hooks/tcpdump.h"
#include "hooks/track.h"
#include "metadata.h"
#include "module.h"
#include "opts.h"
#include "port.h"
#include "tc.h"
#include "utils/ether.h"
#include "utils/time.h"
#include "worker.h"

struct handler_map {
  const char *cmd;
  int pause_needed; /* should all workers have been paused? */
  struct snobj *(*func)(struct snobj *);
};

static const char *resource_names[NUM_RESOURCES] = {"schedules", "cycles",
                                                    "packets", "bits"};

static int name_to_resource(const char *name) {
  for (int i = 0; i < NUM_RESOURCES; i++)
    if (strcmp(resource_names[i], name) == 0)
      return i;

  /* not found */
  return -1;
}

static struct snobj *handle_reset_modules(struct snobj *);
static struct snobj *handle_reset_ports(struct snobj *);
static struct snobj *handle_reset_tcs(struct snobj *);
static struct snobj *handle_reset_workers(struct snobj *);

static struct snobj *handle_reset_all(struct snobj *) {
  struct snobj *r;

  LOG(INFO) << "*** reset_all requested ***";

  r = handle_reset_modules(nullptr);
  if (r)
    return r;

  r = handle_reset_ports(nullptr);
  if (r)
    return r;

  r = handle_reset_tcs(nullptr);
  if (r)
    return r;

  r = handle_reset_workers(nullptr);
  if (r)
    return r;

  return nullptr;
}

static struct snobj *handle_pause_all(struct snobj *) {
  pause_all_workers();
  LOG(INFO) << "*** All workers have been paused ***";
  return nullptr;
}

static struct snobj *handle_resume_all(struct snobj *) {
  LOG(INFO) << "*** Resuming ***";
  resume_all_workers();
  return nullptr;
}

static struct snobj *handle_reset_workers(struct snobj *) {
  destroy_all_workers();
  LOG(INFO) << "*** All workers have been destroyed ***";
  return nullptr;
}

static struct snobj *handle_list_workers(struct snobj *) {
  struct snobj *r;

  r = snobj_list();

  for (int wid = 0; wid < MAX_WORKERS; wid++) {
    struct snobj *worker;

    if (!is_worker_active(wid))
      continue;

    worker = snobj_map();
    snobj_map_set(worker, "wid", snobj_int(wid));
    snobj_map_set(worker, "running", snobj_int(is_worker_running(wid)));
    snobj_map_set(worker, "core", snobj_int(workers[wid]->core()));
    snobj_map_set(worker, "num_tcs", snobj_int(workers[wid]->s()->num_classes));
    snobj_map_set(worker, "silent_drops",
                  snobj_int(workers[wid]->silent_drops()));

    snobj_list_add(r, worker);
  }

  return r;
}

static struct snobj *handle_add_worker(struct snobj *q) {
  unsigned int wid;
  unsigned int core;

  struct snobj *t;

  t = snobj_eval(q, "wid");
  if (!t)
    return snobj_err(EINVAL, "Missing 'wid' field");

  wid = snobj_uint_get(t);
  if (wid >= MAX_WORKERS)
    return snobj_err(EINVAL, "'wid' must be between 0 and %d", MAX_WORKERS - 1);

  t = snobj_eval(q, "core");
  if (!t)
    return snobj_err(EINVAL, "Missing 'core' field");

  core = snobj_uint_get(t);
  if (!is_cpu_present(core))
    return snobj_err(EINVAL, "Invalid core %d", core);

  if (is_worker_active(wid))
    return snobj_err(EEXIST, "worker:%d is already active", wid);

  launch_worker(wid, core);

  return nullptr;
}

static struct snobj *handle_reset_tcs(struct snobj *) {
  for (const auto &it : TCContainer::tcs) {
    struct tc *c = it.second;

    if (c->num_tasks) {
      return snobj_err(EBUSY, "TC %s still has %d tasks",
                       c->settings.name.c_str(), c->num_tasks);
    }

    if (c->settings.auto_free)
      continue;

    tc_leave(c);
    tc_dec_refcnt(c);
  }

  return nullptr;
}

static struct snobj *handle_list_tcs(struct snobj *q) {
  struct snobj *r;
  struct snobj *t;

  unsigned int wid_filter = MAX_WORKERS;

  t = snobj_eval(q, "wid");
  if (t) {
    wid_filter = snobj_uint_get(t);

    if (wid_filter >= MAX_WORKERS)
      return snobj_err(EINVAL, "'wid' must be between 0 and %d",
                       MAX_WORKERS - 1);

    if (!is_worker_active(wid_filter))
      return snobj_err(EINVAL, "worker:%d does not exist", wid_filter);
  }

  r = snobj_list();

  for (const auto &it : TCContainer::tcs) {
    struct tc *c = it.second;
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

    struct snobj *elem = snobj_map();

    snobj_map_set(elem, "name", snobj_str(c->settings.name));
    snobj_map_set(elem, "tasks", snobj_int(c->num_tasks));
    snobj_map_set(elem, "parent", snobj_str(c->parent->settings.name));
    snobj_map_set(elem, "priority", snobj_int(c->settings.priority));

    if (wid < MAX_WORKERS)
      snobj_map_set(elem, "wid", snobj_uint(wid));
    else
      snobj_map_set(elem, "wid", snobj_int(-1));

    struct snobj *limit = snobj_map();

    for (int i = 0; i < NUM_RESOURCES; i++) {
      snobj_map_set(limit, resource_names[i], snobj_uint(c->settings.limit[i]));
    }

    snobj_map_set(elem, "limit", limit);

    struct snobj *max_burst = snobj_map();

    for (int i = 0; i < NUM_RESOURCES; i++) {
      snobj_map_set(max_burst, resource_names[i],
                    snobj_uint(c->settings.max_burst[i]));
    }

    snobj_map_set(elem, "max_burst", max_burst);

    snobj_list_add(r, elem);
  }

  return r;
}

static struct snobj *handle_add_tc(struct snobj *q) {
  const char *tc_name;
  int wid;

  struct tc_params params;
  struct tc *c;

  tc_name = snobj_eval_str(q, "name");
  if (!tc_name)
    return snobj_err(EINVAL, "Missing 'name' field");

  if (TCContainer::tcs.count(tc_name))
    return snobj_err(EINVAL, "Name '%s' already exists", tc_name);

  wid = snobj_eval_uint(q, "wid");
  if (wid >= MAX_WORKERS)
    return snobj_err(EINVAL, "'wid' must be between 0 and %d", MAX_WORKERS - 1);

  if (!is_worker_active(wid)) {
    if (num_workers == 0 && wid == 0)
      launch_worker(wid, FLAGS_c);
    else
      return snobj_err(EINVAL, "worker:%d does not exist", wid);
  }

  params.name = tc_name;

  params.priority = snobj_eval_int(q, "priority");
  if (params.priority == DEFAULT_PRIORITY)
    return snobj_err(EINVAL, "Priority %d is reserved", DEFAULT_PRIORITY);

  /* TODO: add support for other parameters */
  params.share = 1;
  params.share_resource = RESOURCE_CNT;

  struct snobj *limit = snobj_eval(q, "limit");
  if (limit) {
    if (snobj_type(limit) != TYPE_MAP)
      return snobj_err(EINVAL, "'limit' must be a map\n");

    for (size_t i = 0; i < limit->size; i++) {
      int rsc = name_to_resource(limit->map.arr_k[i]);

      if (rsc < 0)
        return snobj_err(EINVAL, "Invalid resource name '%s'\n",
                         limit->map.arr_k[i]);

      params.limit[rsc] = snobj_uint_get(limit->map.arr_v[i]);
    }
  }

  struct snobj *max_burst = snobj_eval(q, "max_burst");
  if (max_burst) {
    if (snobj_type(max_burst) != TYPE_MAP)
      return snobj_err(EINVAL, "'max_burst' must be a map\n");

    for (size_t i = 0; i < max_burst->size; i++) {
      int rsc = name_to_resource(max_burst->map.arr_k[i]);

      if (rsc < 0)
        return snobj_err(EINVAL, "Invalid resource name '%s'\n",
                         max_burst->map.arr_k[i]);

      params.max_burst[rsc] = snobj_uint_get(max_burst->map.arr_v[i]);
    }
  }

  c = tc_init(workers[wid]->s(), &params, nullptr);
  if (is_err(c))
    return snobj_err(-ptr_to_err(c), "tc_init() failed");

  tc_join(c);

  return nullptr;
}

static struct snobj *handle_get_tc_stats(struct snobj *q) {
  const char *tc_name;

  struct tc *c;

  struct snobj *r;

  tc_name = snobj_str_get(q);
  if (!tc_name)
    return snobj_err(EINVAL, "Argument must be a name in str");

  const auto &it = TCContainer::tcs.find(tc_name);
  if (it == TCContainer::tcs.end()) {
    return snobj_err(ENOENT, "No TC '%s' found", tc_name);
  }
  c = it->second;

  r = snobj_map();

  snobj_map_set(r, "timestamp", snobj_double(get_epoch_time()));
  snobj_map_set(r, "count", snobj_uint(c->stats.usage[RESOURCE_CNT]));
  snobj_map_set(r, "cycles", snobj_uint(c->stats.usage[RESOURCE_CYCLE]));
  snobj_map_set(r, "packets", snobj_uint(c->stats.usage[RESOURCE_PACKET]));
  snobj_map_set(r, "bits", snobj_uint(c->stats.usage[RESOURCE_BIT]));

  return r;
}

static struct snobj *handle_list_drivers(struct snobj *) {
  struct snobj *r;

  r = snobj_list();

  for (const auto &pair : PortBuilder::all_port_builders()) {
    const PortBuilder &builder = pair.second;
    snobj_list_add(r, snobj_str(builder.class_name()));
  }

  return r;
}

static struct snobj *handle_get_driver_info(struct snobj *q) {
  const char *drv_name;

  struct snobj *r;
  struct snobj *cmds;

  drv_name = snobj_str_get(q);

  if (!drv_name)
    return snobj_err(EINVAL, "Argument must be a name in str");

  const auto &it = PortBuilder::all_port_builders().find(drv_name);
  if (it == PortBuilder::all_port_builders().end()) {
    return snobj_err(ENOENT, "No driver '%s' found", drv_name);
  }

  cmds = snobj_list();
#if 0
	for (int i = 0; i < MAX_COMMANDS; i++) {
		if (!drv->commands[i].cmd)
			break;

		snobj_list_add(cmds, snobj_str(drv->commands[i].cmd));
	}
#endif

  r = snobj_map();
  snobj_map_set(r, "name", snobj_str(it->second.class_name()));
  snobj_map_set(r, "help", snobj_str(it->second.help_text()));
  snobj_map_set(r, "commands", cmds);

  return r;
}

static struct snobj *handle_reset_ports(struct snobj *) {
  for (auto it = PortBuilder::all_ports().cbegin();
       it != PortBuilder::all_ports().end();) {
    auto it_next = std::next(it);
    Port *p = it->second;

    int ret = PortBuilder::DestroyPort(p);
    if (ret)
      return snobj_errno(-ret);

    it = it_next;
  }

  LOG(INFO) << "*** All ports have been destroyed ***";
  return nullptr;
}

static struct snobj *handle_list_ports(struct snobj *) {
  struct snobj *r;

  r = snobj_list();

  for (const auto &pair : PortBuilder::all_ports()) {
    const Port *p = pair.second;
    struct snobj *port = snobj_map();

    snobj_map_set(port, "name", snobj_str(p->name()));
    snobj_map_set(port, "driver", snobj_str(p->port_builder()->class_name()));

    snobj_list_add(r, port);
  }

  return r;
}

/* returns a pointer to the created port.
 * if error, returns nullptr and *perr is set */

static Port *create_port(const char *name, const PortBuilder *driver,
                         struct snobj *arg, struct snobj **perr) {
  std::unique_ptr<Port> p;

  queue_t num_inc_q = 1;
  queue_t num_out_q = 1;

  bool size_inc_q_set = false;
  bool size_out_q_set = false;
  size_t size_inc_q = 0;
  size_t size_out_q = 0;

  bess::utils::EthHeader::Address mac_addr;

  *perr = nullptr;

  if (snobj_eval_exists(arg, "num_inc_q"))
    num_inc_q = snobj_eval_uint(arg, "num_inc_q");

  if (snobj_eval_exists(arg, "num_out_q"))
    num_out_q = snobj_eval_uint(arg, "num_out_q");

  if (snobj_eval_exists(arg, "size_inc_q")) {
    size_inc_q = snobj_eval_uint(arg, "size_inc_q");
    size_inc_q_set = true;
  }

  if (snobj_eval_exists(arg, "size_out_q")) {
    size_out_q = snobj_eval_uint(arg, "size_out_q");
    size_out_q_set = true;
  }

  if (snobj_eval_exists(arg, "mac_addr")) {
    char *v = snobj_eval_str(arg, "mac_addr");

    if (!mac_addr.FromString(v)) {
      *perr = snobj_err(EINVAL,
                        "MAC address should be "
                        "formatted as a string "
                        "xx:xx:xx:xx:xx:xx");
      return nullptr;
    }
  } else
    mac_addr.Randomize();

  if (num_inc_q > MAX_QUEUES_PER_DIR || num_out_q > MAX_QUEUES_PER_DIR) {
    *perr = snobj_err(EINVAL, "Invalid number of queues");
    return nullptr;
  }

  if (size_inc_q > MAX_QUEUE_SIZE || size_out_q > MAX_QUEUE_SIZE) {
    *perr = snobj_err(EINVAL, "Invalid queue size");
    return nullptr;
  }

  std::string port_name;

  if (name) {
    if (PortBuilder::all_ports().count(name)) {
      *perr = snobj_err(EEXIST, "Port '%s' already exists", name);
      return nullptr;
    }

    port_name = name;
  } else {
    port_name = PortBuilder::GenerateDefaultPortName(driver->class_name(),
                                                     driver->name_template());
  }

  // Try to create and initialize the port.
  p.reset(driver->CreatePort(port_name));

  memcpy(p->mac_addr, mac_addr.bytes, ETH_ALEN);
  p->num_queues[PACKET_DIR_INC] = num_inc_q;
  p->num_queues[PACKET_DIR_OUT] = num_out_q;

  if (size_inc_q_set) {
    p->queue_size[PACKET_DIR_INC] = size_inc_q;
  } else {
    p->queue_size[PACKET_DIR_INC] = p->DefaultIncQueueSize();
  }
  if (size_out_q_set) {
    p->queue_size[PACKET_DIR_OUT] = size_out_q;
  } else {
    p->queue_size[PACKET_DIR_OUT] = p->DefaultOutQueueSize();
  }

  *perr = p->Init(arg);
  if (*perr != nullptr) {
    return nullptr;
  }

  if (!PortBuilder::AddPort(p.get())) {
    return nullptr;
  }

  return p.release();
}

static struct snobj *handle_create_port(struct snobj *q) {
  const char *driver_name;
  Port *port;

  struct snobj *r;
  struct snobj *err;

  driver_name = snobj_eval_str(q, "driver");
  if (!driver_name)
    return snobj_err(EINVAL, "Missing 'driver' field");

  const auto &builders = PortBuilder::all_port_builders();
  const auto &it = builders.find(driver_name);
  if (it == builders.end()) {
    return snobj_err(ENOENT, "No port driver '%s' found", driver_name);
  }
  const PortBuilder &builder = it->second;

  port = create_port(snobj_eval_str(q, "name"), &builder, snobj_eval(q, "arg"),
                     &err);
  if (!port)
    return err;

  r = snobj_map();
  snobj_map_set(r, "name", snobj_str(port->name()));

  return r;
}

static struct snobj *handle_destroy_port(struct snobj *q) {
  const char *port_name;

  int ret;

  port_name = snobj_str_get(q);
  if (!port_name)
    return snobj_err(EINVAL, "Argument must be a name in str");

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return snobj_err(ENOENT, "No port `%s' found", port_name);
  }

  ret = PortBuilder::DestroyPort(it->second);
  if (ret)
    return snobj_errno(-ret);

  return nullptr;
}

static struct snobj *handle_get_port_stats(struct snobj *q) {
  const char *port_name;

  port_stats_t stats;

  struct snobj *r;
  struct snobj *inc;
  struct snobj *out;

  port_name = snobj_str_get(q);
  if (!port_name)
    return snobj_err(EINVAL, "Argument must be a name in str");

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return snobj_err(ENOENT, "No port '%s' found", port_name);
  }
  it->second->GetPortStats(&stats);

  inc = snobj_map();
  snobj_map_set(inc, "packets", snobj_uint(stats[PACKET_DIR_INC].packets));
  snobj_map_set(inc, "dropped", snobj_uint(stats[PACKET_DIR_INC].dropped));
  snobj_map_set(inc, "bytes", snobj_uint(stats[PACKET_DIR_INC].bytes));

  out = snobj_map();
  snobj_map_set(out, "packets", snobj_uint(stats[PACKET_DIR_OUT].packets));
  snobj_map_set(out, "dropped", snobj_uint(stats[PACKET_DIR_OUT].dropped));
  snobj_map_set(out, "bytes", snobj_uint(stats[PACKET_DIR_OUT].bytes));

  r = snobj_map();
  snobj_map_set(r, "inc", inc);
  snobj_map_set(r, "out", out);
  snobj_map_set(r, "timestamp", snobj_double(get_epoch_time()));

  return r;
}

static struct snobj *handle_list_mclasses(struct snobj *) {
  struct snobj *r = snobj_list();

  for (const auto &pair : ModuleBuilder::all_module_builders()) {
    const ModuleBuilder &builder = pair.second;
    snobj_list_add(r, snobj_str(builder.class_name()));
  }

  return r;
}

static struct snobj *handle_get_mclass_info(struct snobj *q) {
  const char *cls_name;
  const ModuleBuilder *cls;

  struct snobj *r;
  struct snobj *cmds;

  cls_name = snobj_str_get(q);

  if (!cls_name)
    return snobj_err(EINVAL, "Argument must be a name in str");

  const auto &it = ModuleBuilder::all_module_builders().find(cls_name);
  if (it == ModuleBuilder::all_module_builders().end()) {
    return snobj_err(ENOENT, "No module class '%s' found", cls_name);
  }
  cls = &it->second;

  cmds = snobj_list();

  for (const std::string &cmd : cls->cmds()) {
    snobj_list_add(cmds, snobj_str(cmd));
  }

  r = snobj_map();
  snobj_map_set(r, "name", snobj_str(cls->class_name()));
  snobj_map_set(r, "help", snobj_str(cls->help_text()));
  snobj_map_set(r, "commands", cmds);

  return r;
}

static struct snobj *handle_reset_modules(struct snobj *) {
  ModuleBuilder::DestroyAllModules();
  LOG(INFO) << "*** All modules have been destroyed ***";
  return nullptr;
}

static struct snobj *handle_list_modules(struct snobj *) {
  struct snobj *r;

  r = snobj_list();

  for (const auto &pair : ModuleBuilder::all_modules()) {
    const Module *m = pair.second;
    struct snobj *module = snobj_map();

    snobj_map_set(module, "name", snobj_str(m->name()));
    snobj_map_set(module, "mclass",
                  snobj_str(m->module_builder()->class_name()));
    snobj_map_set(module, "desc", snobj_str(m->GetDesc().c_str()));
    snobj_list_add(r, module);
  }

  return r;
}

// FIXME
static struct snobj *handle_create_module(struct snobj *q) {
  const char *mclass_name;
  const char *name;
  Module *m = nullptr;

  struct snobj *r;
  struct snobj *err;

  mclass_name = snobj_eval_str(q, "mclass");
  if (!mclass_name)
    return snobj_err(EINVAL, "Missing 'mclass' field");

  const auto &builders = ModuleBuilder::all_module_builders();
  const auto &it = builders.find(mclass_name);
  if (it == builders.end()) {
    return snobj_err(ENOENT, "No mclass '%s' found", mclass_name);
  }
  const ModuleBuilder &builder = it->second;

  name = snobj_eval_str(q, "name");
  std::string mod_name;
  if (name) {
    const auto &all_modules = ModuleBuilder::all_modules();
    if (all_modules.find(name) != all_modules.end()) {
      return snobj_err(EEXIST, "Module '%s' already exists", name);
    }
    mod_name = name;
  } else {
    mod_name = ModuleBuilder::GenerateDefaultName(builder.class_name(),
                                                  builder.name_template());
  }

  m = builder.CreateModule(mod_name, &bess::metadata::default_pipeline);

  err = m->Init(snobj_eval(q, "arg"));
  if (err != nullptr) {
    ModuleBuilder::DestroyModule(m);  // XXX: fix me
    return err;
  }

  if (!builder.AddModule(m)) {
    return snobj_err(ENOMEM, "Failed to add module '%s'", name);
    ;
  }

  r = snobj_map();
  snobj_map_set(r, "name", snobj_str(m->name()));

  return r;
}

static struct snobj *handle_destroy_module(struct snobj *q) {
  const char *m_name;

  m_name = snobj_str_get(q);

  if (!m_name)
    return snobj_err(EINVAL, "Argument must be a name in str");

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);

  ModuleBuilder::DestroyModule(it->second);

  return nullptr;
}

static struct snobj *collect_igates(Module *m) {
  struct snobj *igates = snobj_list();

  for (const auto &g : m->igates) {
    if (!g) {
      continue;
    }

    struct snobj *igate = snobj_map();

    struct snobj *ogates = snobj_list();

    snobj_map_set(igate, "igate", snobj_uint(g->gate_idx()));

    TrackGate *t =
        reinterpret_cast<TrackGate *>(g->FindHook(kGateHookTrackGate));
    if (t) {
      snobj_map_set(igate, "cnt", snobj_uint(t->cnt()));
      snobj_map_set(igate, "pkts", snobj_uint(t->pkts()));
      snobj_map_set(igate, "timestamp", snobj_double(get_epoch_time()));
    }

    for (const auto &og : g->ogates_upstream()) {
      struct snobj *ogate = snobj_map();
      snobj_map_set(ogate, "ogate", snobj_uint(og->gate_idx()));
      snobj_map_set(ogate, "name", snobj_str(og->module()->name()));
      snobj_list_add(ogates, ogate);
    }

    snobj_map_set(igate, "ogates", ogates);

    snobj_list_add(igates, igate);
  }

  return igates;
}

static struct snobj *collect_ogates(Module *m) {
  struct snobj *ogates = snobj_list();

  for (const auto &g : m->ogates) {
    if (!g) {
      continue;
    }

    struct snobj *ogate = snobj_map();

    snobj_map_set(ogate, "ogate", snobj_uint(g->gate_idx()));

    TrackGate *t =
        reinterpret_cast<TrackGate *>(g->FindHook(kGateHookTrackGate));
    if (t) {
      snobj_map_set(ogate, "cnt", snobj_uint(t->cnt()));
      snobj_map_set(ogate, "pkts", snobj_uint(t->pkts()));
      snobj_map_set(ogate, "timestamp", snobj_double(get_epoch_time()));
    }

    snobj_map_set(ogate, "name", snobj_str(g->igate()->module()->name()));
    snobj_map_set(ogate, "igate", snobj_uint(g->igate()->gate_idx()));

    snobj_list_add(ogates, ogate);
  }

  return ogates;
}

static struct snobj *collect_metadata(Module *m) {
  struct snobj *metadata = snobj_list();
  size_t i = 0;

  for (const auto &it : m->all_attrs()) {
    struct snobj *attr = snobj_map();

    snobj_map_set(attr, "name", snobj_str(it.name));
    snobj_map_set(attr, "size", snobj_uint(it.size));

    switch (it.mode) {
      case bess::metadata::Attribute::AccessMode::kRead:
        snobj_map_set(attr, "mode", snobj_str("read"));
        break;
      case bess::metadata::Attribute::AccessMode::kWrite:
        snobj_map_set(attr, "mode", snobj_str("write"));
        break;
      case bess::metadata::Attribute::AccessMode::kUpdate:
        snobj_map_set(attr, "mode", snobj_str("update"));
        break;
      default:
        assert(0);
    }

    snobj_map_set(attr, "offset", snobj_int(m->attr_offsets[i]));
    snobj_list_add(metadata, attr);
    i++;
  }

  return metadata;
}

static struct snobj *handle_get_module_info(struct snobj *q) {
  const char *m_name;
  Module *m;

  struct snobj *r;

  m_name = snobj_str_get(q);

  if (!m_name)
    return snobj_err(EINVAL, "Argument must be a name in str");

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);

  m = it->second;
  r = snobj_map();

  snobj_map_set(r, "name", snobj_str(m->name()));
  snobj_map_set(r, "mclass", snobj_str(m->module_builder()->class_name()));

  snobj_map_set(r, "desc", snobj_str(m->GetDesc().c_str()));
  snobj_map_set(r, "dump", m->GetDump());

  snobj_map_set(r, "igates", collect_igates(m));
  snobj_map_set(r, "ogates", collect_ogates(m));
  snobj_map_set(r, "metadata", collect_metadata(m));

  return r;
}

static struct snobj *handle_connect_modules(struct snobj *q) {
  const char *m1_name;
  const char *m2_name;
  gate_idx_t ogate;
  gate_idx_t igate;

  Module *m1;
  Module *m2;

  int ret;

  m1_name = snobj_eval_str(q, "m1");
  m2_name = snobj_eval_str(q, "m2");
  ogate = snobj_eval_uint(q, "ogate");
  igate = snobj_eval_uint(q, "igate");

  if (!m1_name || !m2_name)
    return snobj_err(EINVAL, "Missing 'm1' or 'm2' field");

  const auto &it1 = ModuleBuilder::all_modules().find(m1_name);
  if (it1 == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m1_name);
  m1 = it1->second;

  const auto &it2 = ModuleBuilder::all_modules().find(m2_name);
  if (it2 == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m2_name);
  m2 = it2->second;

  ret = m1->ConnectModules(ogate, m2, igate);

  if (ret < 0)
    return snobj_err(-ret, "Connection %s:%d->%d:%s failed", m1_name, ogate,
                     igate, m2_name);

  return nullptr;
}

static struct snobj *handle_disconnect_modules(struct snobj *q) {
  const char *m_name;
  gate_idx_t ogate;

  Module *m;

  int ret;

  m_name = snobj_eval_str(q, "name");
  ogate = snobj_eval_uint(q, "ogate");

  if (!m_name)
    return snobj_err(EINVAL, "Missing 'name' field");

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);
  m = it->second;

  ret = m->DisconnectModules(ogate);
  if (ret < 0)
    return snobj_err(-ret, "Disconnection %s:%d failed", m_name, ogate);

  return nullptr;
}

static struct snobj *handle_attach_task(struct snobj *q) {
  const char *m_name;
  const char *tc_name;

  task_id_t tid;

  Module *m;
  struct task *t;

  m_name = snobj_eval_str(q, "name");

  if (!m_name)
    return snobj_err(EINVAL, "Missing 'name' field");

  {
    const auto &it = ModuleBuilder::all_modules().find(m_name);
    if (it == ModuleBuilder::all_modules().end())
      return snobj_err(ENOENT, "No module '%s' found", m_name);
    m = it->second;
  }

  tid = snobj_eval_uint(q, "taskid");
  if (tid >= MAX_TASKS_PER_MODULE)
    return snobj_err(EINVAL, "'taskid' must be between 0 and %d",
                     MAX_TASKS_PER_MODULE - 1);

  if ((t = m->tasks[tid]) == nullptr)
    return snobj_err(ENOENT, "Task %s:%hu does not exist", m_name, tid);

  tc_name = snobj_eval_str(q, "tc");

  if (tc_name) {
    struct tc *c;

    const auto &it = TCContainer::tcs.find(tc_name);
    if (it == TCContainer::tcs.end()) {
      return snobj_err(ENOENT, "No TC '%s' found", tc_name);
    }
    c = it->second;

    task_attach(t, c);
  } else {
    int wid; /* TODO: worker_id_t */

    if (task_is_attached(t))
      return snobj_err(EBUSY,
                       "Task %s:%hu is already "
                       "attached to a TC",
                       m_name, tid);

    wid = snobj_eval_uint(q, "wid");
    if (wid >= MAX_WORKERS)
      return snobj_err(EINVAL, "'wid' must be between 0 and %d",
                       MAX_WORKERS - 1);

    if (!is_worker_active(wid))
      return snobj_err(EINVAL, "Worker %d does not exist", wid);

    assign_default_tc(wid, t);
  }

  return nullptr;
}

static struct snobj *handle_enable_tcpdump(struct snobj *q) {
  const char *m_name;
  const char *fifo;
  gate_idx_t gate;
  int is_igate;

  Module *m;

  int ret;

  m_name = snobj_eval_str(q, "name");
  gate = snobj_eval_uint(q, "gate");
  is_igate = snobj_eval_int(q, "is_igate");
  fifo = snobj_eval_str(q, "fifo");

  if (!m_name)
    return snobj_err(EINVAL, "Missing 'name' field");

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);
  m = it->second;

  if (!is_igate && gate >= m->ogates.size())
    return snobj_err(EINVAL, "Output gate '%hu' does not exist", gate);

  if (is_igate && gate >= m->igates.size())
    return snobj_err(EINVAL, "Input gate '%hu' does not exist", gate);

  ret = m->EnableTcpDump(fifo, is_igate, gate);

  if (ret < 0) {
    return snobj_err(-ret, "Enabling tcpdump %s:%d failed", m_name, gate);
  }

  return nullptr;
}

static struct snobj *handle_disable_tcpdump(struct snobj *q) {
  const char *m_name;
  gate_idx_t gate;
  int is_igate;

  Module *m;

  int ret;

  m_name = snobj_eval_str(q, "name");
  gate = snobj_eval_uint(q, "gate");
  is_igate = snobj_eval_int(q, "is_igate");

  if (!m_name)
    return snobj_err(EINVAL, "Missing 'name' field");

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);
  m = it->second;

  if (!is_igate && gate >= m->ogates.size())
    return snobj_err(EINVAL, "Output gate '%hu' does not exist", gate);

  if (is_igate && gate >= m->igates.size())
    return snobj_err(EINVAL, "Input gate '%hu' does not exist", gate);

  ret = m->DisableTcpDump(is_igate, gate);

  if (ret < 0) {
    return snobj_err(-ret, "Disabling tcpdump %s:%d failed", m_name, gate);
  }
  return nullptr;
}

static struct snobj *enable_track_for_module(const Module *m,
                                             struct snobj *gate_idx_,
                                             int is_igate) {
  int ret;

  if (gate_idx_) {
    gate_idx_t gate_idx = snobj_uint_get(gate_idx_);

    if (!is_igate && gate_idx >= m->ogates.size()) {
      return snobj_err(EINVAL, "Output gate '%hu' does not exist", gate_idx);
    }

    if (is_igate && gate_idx >= m->igates.size()) {
      return snobj_err(EINVAL, "Input gate '%hu' does not exist", gate_idx);
    }

    if (is_igate && (ret = m->igates[gate_idx]->AddHook(new TrackGate()))) {
      return snobj_err(ret, "Failed to track input gate '%hu'", gate_idx);
    }

    if ((ret = m->ogates[gate_idx]->AddHook(new TrackGate()))) {
      return snobj_err(ret, "Failed to track output gate '%hu'", gate_idx);
    }
  }

  // XXX: ewwwwww
  if (is_igate) {
    for (auto &gate : m->igates) {
      if ((ret = gate->AddHook(new TrackGate()))) {
        return snobj_err(ret, "Failed to track input gate '%hu'",
                         gate->gate_idx());
      }
    }
  } else {
    for (auto &gate : m->ogates) {
      if ((ret = gate->AddHook(new TrackGate()))) {
        return snobj_err(ret, "Failed to track output gate '%hu'",
                         gate->gate_idx());
      }
    }
  }
  return nullptr;
}

static struct snobj *handle_enable_track(struct snobj *q) {
  struct snobj *ret;
  const char *m_name;
  int is_igate;

  m_name = snobj_eval_str(q, "name");
  is_igate = snobj_eval_int(q, "is_igate");

  if (!m_name) {
    for (const auto &it : ModuleBuilder::all_modules()) {
      ret = enable_track_for_module(it.second, snobj_map_get(q, "gate"),
                                    is_igate);
      if (ret) {
        return ret;  // XXX: would it be better to just log here?
      }
    }
    return nullptr;
  }

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);
  return enable_track_for_module(it->second, snobj_map_get(q, "gate"),
                                 is_igate);
}

static struct snobj *disable_track_for_module(const Module *m,
                                              struct snobj *gate_idx_,
                                              int is_igate) {
  if (gate_idx_) {
    gate_idx_t gate_idx = snobj_uint_get(gate_idx_);

    if (!is_igate && gate_idx >= m->ogates.size()) {
      return snobj_err(EINVAL, "Output gate '%hu' does not exist", gate_idx);
    }

    if (is_igate && gate_idx >= m->igates.size()) {
      return snobj_err(EINVAL, "Input gate '%hu' does not exist", gate_idx);
    }

    if (is_igate) {
      m->igates[gate_idx]->RemoveHook(kGateHookTrackGate);
      return nullptr;
    }
    m->ogates[gate_idx]->RemoveHook(kGateHookTrackGate);
    return nullptr;
  }

  // XXX: ewwwwww
  if (is_igate) {
    for (auto &gate : m->igates) {
      gate->RemoveHook(kGateHookTrackGate);
    }
  } else {
    for (auto &gate : m->ogates) {
      gate->RemoveHook(kGateHookTrackGate);
    }
  }
  return nullptr;
}

static struct snobj *handle_disable_track(struct snobj *q) {
  struct snobj *ret;
  const char *m_name;
  int is_igate;

  m_name = snobj_eval_str(q, "name");
  is_igate = snobj_eval_int(q, "is_igate");

  if (!m_name) {
    for (const auto &it : ModuleBuilder::all_modules()) {
      ret = disable_track_for_module(it.second, snobj_map_get(q, "gate"),
                                     is_igate);
      if (ret) {
        return ret;  // XXX: would it be better to just log here?
      }
    }
    return nullptr;
  }

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);
  return disable_track_for_module(it->second, snobj_map_get(q, "gate"),
                                  is_igate);

  return nullptr;
}

/* Adding this mostly to provide a reasonable way to exit when daemonized */
static struct snobj *handle_kill_bess(struct snobj *) {
  LOG(WARNING) << "Halt requested by a client";
  destroy_all_workers();
  exit(EXIT_SUCCESS);

  /* Never called */
  return nullptr;
}

static struct snobj *handle_not_implemented(struct snobj *) {
  return snobj_err(ENOTSUP, "Not implemented yet");
}

static struct handler_map sn_handlers[] = {
    /* remove all ports/modules/TCs/workers */
    {"reset_all", 1, handle_reset_all},

    /* pause and resume all workers */
    {"pause_all", 0, handle_pause_all},
    {"resume_all", 0, handle_resume_all},

    {"reset_workers", 1, handle_reset_workers},
    {"list_workers", 0, handle_list_workers},
    {"add_worker", 0, handle_add_worker},
    {"delete_worker", 1, handle_not_implemented},

    {"reset_tcs", 1, handle_reset_tcs},
    {"list_tcs", 0, handle_list_tcs},
    {"add_tc", 1, handle_add_tc},
    {"get_tc_stats", 0, handle_get_tc_stats},

    {"list_drivers", 0, handle_list_drivers},
    {"get_driver_info", 0, handle_get_driver_info},
    {"import_driver", 0, handle_not_implemented}, /* TODO */

    {"reset_ports", 1, handle_reset_ports},
    {"list_ports", 0, handle_list_ports},
    {"create_port", 0, handle_create_port},
    {"destroy_port", 0, handle_destroy_port},
    {"get_port_stats", 0, handle_get_port_stats},

    {"list_mclasses", 0, handle_list_mclasses},
    {"get_mclass_info", 0, handle_get_mclass_info},
    {"import_mclass", 0, handle_not_implemented}, /* TODO */

    {"reset_modules", 1, handle_reset_modules},
    {"list_modules", 0, handle_list_modules},
    {"create_module", 1, handle_create_module},
    {"destroy_module", 1, handle_destroy_module},
    {"get_module_info", 0, handle_get_module_info},
    {"connect_modules", 1, handle_connect_modules},
    {"disconnect_modules", 1, handle_disconnect_modules},

    {"attach_task", 1, handle_attach_task},

    {"enable_tcpdump", 1, handle_enable_tcpdump},
    {"disable_tcpdump", 1, handle_disable_tcpdump},

    {"enable_track", 1, handle_enable_track},
    {"disable_track", 1, handle_disable_track},

    {"kill_bess", 1, handle_kill_bess},

    {nullptr, 0, nullptr}};

static struct snobj *handle_snobj_bess(struct snobj *q) {
  struct snobj *arg;
  const char *s;

  s = snobj_eval_str(q, "cmd");
  if (!s)
    return snobj_err(EINVAL, "Missing 'cmd' field");

  arg = snobj_map_get(q, "arg");

  for (int i = 0; sn_handlers[i].cmd != nullptr; i++) {
    if (strcmp(s, sn_handlers[i].cmd) != 0)
      continue;

    if (sn_handlers[i].pause_needed && is_any_worker_running())
      return snobj_err(EBUSY, "There is a running worker");

    return sn_handlers[i].func(arg);
  }

  return snobj_err(ENOTSUP, "Unknown command in 'cmd': '%s'", s);
}

struct snobj *run_module_command(Module *m, const char *cmd,
                                 struct snobj *arg) {
  return m->RunCommand(cmd, arg);
#if 0
	for (int i = 0; i < MAX_COMMANDS; i++) {
		if (!cls->commands[i].cmd)
			break;

		if (strcmp(cls->commands[i].cmd, cmd) == 0) {
			if (!cls->commands[i].mt_safe &&
					is_any_worker_running())
			{
				return snobj_err(EBUSY,
						"There is a running worker and "
						"command '%s' is not MT safe",
						cmd);
			}

			return cls->commands[i].func(m, cmd, arg);
		}
	}

	return snobj_err(ENOTSUP, "'%s' does not support command '%s'",
			cls->name, cmd);
#endif
}

static struct snobj *handle_snobj_module(struct snobj *q) {
  const char *m_name;
  const char *cmd;

  Module *m;

  struct snobj *arg;

  m_name = snobj_eval_str(q, "name");
  if (!m_name)
    return snobj_err(EINVAL, "Missing module name field 'name'");

  const auto &it = ModuleBuilder::all_modules().find(m_name);
  if (it == ModuleBuilder::all_modules().end())
    return snobj_err(ENOENT, "No module '%s' found", m_name);
  m = it->second;

  cmd = snobj_eval_str(q, "cmd");
  if (!cmd)
    return snobj_err(EINVAL, "Missing command name field 'cmd'");

  arg = snobj_eval(q, "arg");
  if (!arg) {
    struct snobj *ret;

    arg = snobj_nil();
    ret = run_module_command(m, cmd, arg);
    snobj_free(arg);
    return ret;
  } else
    return run_module_command(m, cmd, arg);
}

struct snobj *handle_request(struct snobj *q) {
  struct snobj *r = nullptr;
  const char *s;

  if (FLAGS_d) {
    LOG(INFO) << "Request:" << std::endl << snobj_dump(q);
  }

  if (q->type != TYPE_MAP) {
    r = snobj_err(EINVAL, "The message must be a map");
    goto reply;
  }

  s = snobj_eval_str(q, "to");
  if (!s) {
    r = snobj_str("There is no 'to' field");
    goto reply;
  }

  if (strcmp(s, "bess") == 0) {
    r = handle_snobj_bess(q);
  } else if (strcmp(s, "module") == 0) {
    r = handle_snobj_module(q);
  } else
    r = snobj_err(EINVAL, "Unknown destination in 'to': %s", s);

reply:
  /* No response was made? (normally means "success") */
  if (!r)
    r = snobj_nil();

  if (FLAGS_d) {
    LOG(INFO) << "Response:" << std::endl << snobj_dump(r);
  }

  return r;
}
