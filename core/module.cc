#include "module.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <glog/logging.h>

#include <algorithm>
#include <sstream>

#include "gate.h"
#include "hooks/tcpdump.h"
#include "hooks/track.h"
#include "mem_alloc.h"
#include "scheduler.h"
#include "utils/pcap.h"

const Commands Module::cmds;

std::map<std::string, Module *> ModuleBuilder::all_modules_;

Module *ModuleBuilder::CreateModule(const std::string &name,
                                    bess::metadata::Pipeline *pipeline) const {
  Module *m = module_generator_();
  m->set_name(name);
  m->set_module_builder(this);
  m->set_pipeline(pipeline);
  return m;
}

bool ModuleBuilder::AddModule(Module *m) {
  return all_modules_.insert({m->name(), m}).second;
}

int ModuleBuilder::DestroyModule(Module *m, bool erase) {
  int ret;
  m->DeInit();

  // disconnect from upstream modules.
  for (size_t i = 0; i < m->igates_.size(); i++) {
    ret = m->DisconnectModulesUpstream(i);
    if (ret) {
      return ret;
    }
  }

  // disconnect downstream modules
  for (size_t i = 0; i < m->ogates_.size(); i++) {
    ret = m->DisconnectModules(i);
    if (ret) {
      return ret;
    }
  }

  m->DestroyAllTasks();
  m->DeregisterAllAttributes();

  if (erase) {
    all_modules_.erase(m->name());
  }

  delete m;
  return 0;
}

void ModuleBuilder::DestroyAllModules() {
  int ret;
  for (auto it = all_modules_.begin(); it != all_modules_.end();) {
    auto it_next = std::next(it);
    ret = DestroyModule(it->second, false);
    if (ret) {
      LOG(ERROR) << "Error destroying module '" << it->first
                 << "' (errno = " << ret << ")";
    } else {
      all_modules_.erase(it);
    }
    it = it_next;
  }
}

bool ModuleBuilder::RegisterModuleClass(
    std::function<Module *()> module_generator, const std::string &class_name,
    const std::string &name_template, const std::string &help_text,
    const gate_idx_t igates, const gate_idx_t ogates, const Commands &cmds,
    module_init_func_t init_func) {
  all_module_builders_holder().emplace(
      std::piecewise_construct, std::forward_as_tuple(class_name),
      std::forward_as_tuple(module_generator, class_name, name_template,
                            help_text, igates, ogates, cmds, init_func));
  return true;
}

std::string ModuleBuilder::GenerateDefaultName(
    const std::string &class_name, const std::string &default_template) {
  std::string name_template;

  if (default_template == "") {
    std::ostringstream ss;
    char last_char = '\0';
    for (auto t : class_name) {
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

    if (!all_modules_.count(name))
      return name;
  }

  promise_unreachable();
}

std::map<std::string, ModuleBuilder> &ModuleBuilder::all_module_builders_holder(
    bool reset) {
  // Maps from class names to port builders.  Tracks all port classes (via their
  // PortBuilders).
  static std::map<std::string, ModuleBuilder> all_module_builders;

  if (reset) {
    all_module_builders.clear();
  }

  return all_module_builders;
}

const std::map<std::string, ModuleBuilder>
    &ModuleBuilder::all_module_builders() {
  return all_module_builders_holder();
}

const std::map<std::string, Module *> &ModuleBuilder::all_modules() {
  return all_modules_;
}

pb_cmd_response_t ModuleBuilder::RunCommand(
    Module *m, const std::string &user_cmd,
    const google::protobuf::Any &arg) const {
  pb_cmd_response_t response;
  for (auto &cmd : cmds_) {
    if (user_cmd == cmd.cmd) {
      if (!cmd.mt_safe && is_any_worker_running()) {
        set_cmd_response_error(&response,
                               pb_error(EBUSY,
                                        "There is a running worker "
                                        "and command '%s' is not MT safe",
                                        cmd.cmd.c_str()));
        return response;
      }

      return cmd.func(m, arg);
    }
  }

  set_cmd_response_error(&response,
                         pb_error(ENOTSUP, "'%s' does not support command '%s'",
                                  class_name_.c_str(), user_cmd.c_str()));
  return response;
}

pb_error_t ModuleBuilder::RunInit(Module *m,
                                  const google::protobuf::Any &arg) const {
  return init_func_(m, arg);
}

// -------------------------------------------------------------------------
pb_error_t Module::InitWithGenericArg(const google::protobuf::Any &arg) {
  return module_builder_->RunInit(this, arg);
}

pb_error_t Module::Init(const bess::pb::EmptyArg &) {
  return pb_errno(0);
}

void Module::DeInit() {}

struct task_result Module::RunTask(void *) {
  CHECK(0);  // You must override this function
  return task_result();
}

void Module::ProcessBatch(bess::PacketBatch *) {
  CHECK(0);  // You must override this function
}

task_id_t Module::RegisterTask(void *arg) {
  Worker *w = get_next_active_worker();
  bess::LeafTrafficClass *c = w->scheduler()->default_leaf_class();

  tasks_.push_back(new Task(this, arg, c));
  return tasks_.size() - 1;
}

void Module::DestroyAllTasks() {
  for (auto task : tasks_) {
    delete task;
  }
  tasks_.clear();
}

void Module::DeregisterAllAttributes() {
  for (const auto &it : attrs_) {
    pipeline_->DeregisterAttribute(it.name);
  }
}

int Module::AddMetadataAttr(const std::string &name, size_t size,
                            bess::metadata::Attribute::AccessMode mode) {
  int ret;

  if (attrs_.size() >= bess::metadata::kMaxAttrsPerModule)
    return -ENOSPC;

  if (name.empty())
    return -EINVAL;

  if (size < 1 || size > bess::metadata::kMetadataAttrMaxSize)
    return -EINVAL;

  // We do not allow a module to have multiple attributes with the same name
  for (const auto &it : attrs_) {
    if (it.name == name) {
      return -EEXIST;
    }
  }

  if ((ret = pipeline_->RegisterAttribute(name, size))) {
    return ret;
  }

  bess::metadata::Attribute attr;
  attr.name = name;
  attr.size = size;
  attr.mode = mode;
  attr.scope_id = -1;

  attrs_.push_back(attr);

  return attrs_.size() - 1;
}

/* returns -errno if fails */
int Module::ConnectModules(gate_idx_t ogate_idx, Module *m_next,
                           gate_idx_t igate_idx) {
  bess::OGate *ogate;
  bess::IGate *igate;

  if (ogate_idx >= module_builder_->NumOGates() || ogate_idx >= MAX_GATES) {
    return -EINVAL;
  }

  if (igate_idx >= m_next->module_builder()->NumIGates() ||
      igate_idx >= MAX_GATES) {
    return -EINVAL;
  }

  /* already being used? */
  if (is_active_gate<bess::OGate>(ogates_, ogate_idx)) {
    return -EBUSY;
  }

  if (ogate_idx >= ogates_.size()) {
    ogates_.resize(ogate_idx + 1, nullptr);
  }

  ogate = new bess::OGate(this, ogate_idx, m_next);
  if (!ogate) {
    return -ENOMEM;
  }
  ogates_[ogate_idx] = ogate;

  if (igate_idx >= m_next->igates_.size()) {
    m_next->igates_.resize(igate_idx + 1, nullptr);
  }

  if (m_next->igates_[igate_idx] == nullptr) {
    igate = new bess::IGate(m_next, igate_idx, m_next);
    m_next->igates_[igate_idx] = igate;
  } else {
    igate = m_next->igates_[igate_idx];
  }

  ogate->set_igate(igate);
  ogate->set_igate_idx(igate_idx);

  // Gate tracking is enabled by default
  ogate->AddHook(new TrackGate());
  igate->PushOgate(ogate);

  return 0;
}

int Module::DisconnectModules(gate_idx_t ogate_idx) {
  bess::OGate *ogate;
  bess::IGate *igate;

  if (ogate_idx >= module_builder_->NumOGates()) {
    return -EINVAL;
  }

  /* no error even if the ogate is unconnected already */
  if (!is_active_gate<bess::OGate>(ogates_, ogate_idx)) {
    return 0;
  }

  ogate = ogates_[ogate_idx];
  if (!ogate) {
    return 0;
  }

  igate = ogate->igate();

  /* Does the igate become inactive as well? */
  igate->RemoveOgate(ogate);
  if (igate->ogates_upstream().empty()) {
    Module *m_next = igate->module();
    m_next->igates_[igate->gate_idx()] = nullptr;
    igate->ClearHooks();
    delete igate;
  }

  ogates_[ogate_idx] = nullptr;
  ogate->ClearHooks();
  delete ogate;

  return 0;
}

int Module::DisconnectModulesUpstream(gate_idx_t igate_idx) {
  bess::IGate *igate;

  if (igate_idx >= module_builder_->NumIGates()) {
    return -EINVAL;
  }

  /* no error even if the igate is unconnected already */
  if (!is_active_gate<bess::IGate>(igates_, igate_idx)) {
    return 0;
  }

  igate = igates_[igate_idx];
  if (!igate) {
    return 0;
  }

  for (const auto &ogate : igate->ogates_upstream()) {
    Module *m_prev = ogate->module();
    m_prev->ogates_[ogate->gate_idx()] = nullptr;
    ogate->ClearHooks();
    delete ogate;
  }

  igates_[igate_idx] = nullptr;
  igate->ClearHooks();
  delete igate;

  return 0;
}

void Module::RunSplit(const gate_idx_t *out_gates,
                      bess::PacketBatch *mixed_batch) {
  int cnt = mixed_batch->cnt();
  int num_pending = 0;

  bess::Packet **p_pkt = &mixed_batch->pkts()[0];

  gate_idx_t pending[bess::PacketBatch::kMaxBurst];
  bess::PacketBatch batches[bess::PacketBatch::kMaxBurst];

  bess::PacketBatch *splits = ctx.splits();

  /* phase 1: collect unique ogates into pending[] */
  for (int i = 0; i < cnt; i++) {
    bess::PacketBatch *batch;
    gate_idx_t ogate;

    ogate = out_gates[i];
    batch = &splits[ogate];

    batch->add(*(p_pkt++));

    pending[num_pending] = ogate;
    num_pending += (batch->cnt() == 1);
  }

  /* phase 2: move batches to local stack, since it may be reentrant */
  for (int i = 0; i < num_pending; i++) {
    bess::PacketBatch *batch;

    batch = &splits[pending[i]];
    batches[i].Copy(batch);
    batch->clear();
  }

  /* phase 3: fire */
  for (int i = 0; i < num_pending; i++)
    RunChooseModule(pending[i], &batches[i]);
}

#if SN_TRACE_MODULES
#define MAX_TRACE_DEPTH 32
#define MAX_TRACE_BUFSIZE 4096

struct callstack {
  int depth;

  int newlined;
  int indent[MAX_TRACE_DEPTH];
  int curr_indent;

  int buflen;
  char buf[MAX_TRACE_BUFSIZE];
};

__thread struct callstack worker_callstack;

void _trace_start(Module *mod, char *type) {
  struct callstack *s = &worker_callstack;

  DCHECK_EQ(s->depth, 0);
  DCHECK_EQ(s->buflen, 0);

  s->buflen = snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
                       "Worker %d %-8s | %s", current_wid, type, mod->name());

  s->curr_indent = s->buflen;
}

void _trace_end(int print_out) {
  struct callstack *s = &worker_callstack;

  DCHECK_EQ(s->depth, 0);
  s->buflen = 0;
  s->newlined = 0;

  if (print_out) {
    DLOG(INFO) << s->buf;
  }
}

void _trace_before_call(Module *mod, Module *next, bess::PacketBatch *batch) {
  struct callstack *s = &worker_callstack;
  int len;

  s->indent[s->depth] = s->curr_indent;

  if (s->newlined) {
    s->buflen += snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
                          "%*s", s->curr_indent, "");
  }

  len = snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
                 " ---(%d)--> %s", batch->cnt, next->name);

  s->buflen += len;
  s->curr_indent += len;

  s->depth++;
  DCHECK_LT(s->depth, MAX_TRACE_DEPTH);

  s->newlined = 0;
}

void _trace_after_call(void) {
  struct callstack *s = &worker_callstack;

  s->depth--;

  if (!s->newlined) {
    s->newlined = 1;

    s->buflen +=
        snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen, "\n");
  }

  s->curr_indent = s->indent[s->depth];
}
#endif

// TODO(melvin): Much of this belongs in the TcpDump constructor.
int Module::EnableTcpDump(const char *fifo, int is_igate, gate_idx_t gate_idx) {
  static const struct pcap_hdr PCAP_FILE_HDR = {
      .magic_number = PCAP_MAGIC_NUMBER,
      .version_major = PCAP_VERSION_MAJOR,
      .version_minor = PCAP_VERSION_MINOR,
      .thiszone = PCAP_THISZONE,
      .sigfigs = PCAP_SIGFIGS,
      .snaplen = PCAP_SNAPLEN,
      .network = PCAP_NETWORK,
  };
  bess::Gate *gate;

  int fd;
  int ret;

  /* Don't allow tcpdump to be attached to gates that are not active */
  if (!is_igate && !is_active_gate<bess::OGate>(ogates_, gate_idx))
    return -EINVAL;

  if (is_igate && !is_active_gate<bess::IGate>(igates_, gate_idx))
    return -EINVAL;

  fd = open(fifo, O_WRONLY | O_NONBLOCK);
  if (fd < 0)
    return -errno;

  /* Looooong time ago Linux ignored O_NONBLOCK in open().
   * Try again just in case. */
  ret = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  if (ret < 0) {
    close(fd);
    return -errno;
  }

  ret = write(fd, &PCAP_FILE_HDR, sizeof(PCAP_FILE_HDR));
  if (ret < 0) {
    close(fd);
    return -errno;
  }

  if (is_igate) {
    gate = igates_[gate_idx];
  } else {
    gate = ogates_[gate_idx];
  }

  TcpDump *tcpdump = new TcpDump();
  if (!gate->AddHook(tcpdump)) {
    tcpdump->set_fifo_fd(fd);
  } else {
    delete tcpdump;
  }

  return 0;
}

int Module::DisableTcpDump(int is_igate, gate_idx_t gate_idx) {
  if (!is_igate && !is_active_gate<bess::OGate>(ogates_, gate_idx))
    return -EINVAL;

  if (is_igate && !is_active_gate<bess::IGate>(igates_, gate_idx))
    return -EINVAL;

  bess::Gate *gate;
  if (is_igate) {
    gate = igates_[gate_idx];
  } else {
    gate = ogates_[gate_idx];
  }

  gate->RemoveHook(kGateHookTcpDumpGate);

  return 0;
}
