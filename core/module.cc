#include "module.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <initializer_list>
#include <sstream>

#include <glog/logging.h>

#include "mem_alloc.h"
#include "utils/pcap.h"
#include "utils/time.h"

std::map<std::string, Module *> ModuleBuilder::all_modules_;

// FIXME: move somewhere else?
void deadend(struct pkt_batch *batch) {
  ctx.incr_silent_drops(batch->cnt);
  snb_free_bulk(batch->pkts, batch->cnt);
}

// FIXME: move somewhere else?
task_id_t task_to_tid(struct task *t) {
  Module *m = t->m;

  for (task_id_t id = 0; id < MAX_TASKS_PER_MODULE; id++)
    if (m->tasks[id] == t)
      return id;

  return INVALID_TASK_ID;
}

// -------------------------------------------------------------------------

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
  m->Deinit();

  // disconnect from upstream modules.
  for (int i = 0; i < m->igates.curr_size; i++) {
    ret = m->DisconnectModulesUpstream(i);
    if (ret)
      return ret;
  }

  // disconnect downstream modules
  for (gate_idx_t i = 0; i < m->ogates.curr_size; i++) {
    ret = m->DisconnectModules(i);
    if (ret)
      return ret;
  }

  m->DestroyAllTasks();

  if (erase)
    all_modules_.erase(m->name());

  mem_free(m->ogates.arr);
  mem_free(m->igates.arr);
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
    const gate_idx_t igates, const gate_idx_t ogates,
    const Commands<Module> &cmds, const PbCommands &pb_cmds,
    module_init_func_t init_func) {
  all_module_builders_holder().emplace(
      std::piecewise_construct, std::forward_as_tuple(class_name),
      std::forward_as_tuple(module_generator, class_name, name_template,
                            help_text, igates, ogates, cmds, pb_cmds,
                            init_func));
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

void deadend(Module *, struct pkt_batch *batch) {
  ctx.incr_silent_drops(batch->cnt);
  snb_free_bulk(batch->pkts, batch->cnt);
}

const std::map<std::string, Module *> &ModuleBuilder::all_modules() {
  return all_modules_;
}

// -------------------------------------------------------------------------
pb_error_t Module::Init(const google::protobuf::Any &arg) {
  return module_builder_->RunInit(this, arg);
}

pb_error_t Module::InitPb(const bess::pb::EmptyArg &) {
  return pb_errno(0);
}

struct snobj *Module::Init(struct snobj *) {
  return nullptr;
}

struct task_result Module::RunTask(void *) {
  assert(0);  // You must override this function
}

void Module::ProcessBatch(struct pkt_batch *) {
  assert(0);  // You must override this function
}

task_id_t Module::RegisterTask(void *arg) {
  task_id_t id;
  struct task *t;

  for (id = 0; id < MAX_TASKS_PER_MODULE; id++)
    if (tasks[id] == nullptr)
      goto found;

  /* cannot find an empty slot */
  return INVALID_TASK_ID;

found:
  t = task_create(this, arg);
  if (!t)
    return INVALID_TASK_ID;

  tasks[id] = t;

  return id;
}

int Module::NumTasks() {
  int cnt = 0;

  for (task_id_t id = 0; id < MAX_TASKS_PER_MODULE; id++)
    if (tasks[id])
      cnt++;

  return cnt;
}

void Module::DestroyAllTasks() {
  for (task_id_t i = 0; i < MAX_TASKS_PER_MODULE; i++) {
    if (tasks[i]) {
      task_destroy(tasks[i]);
      tasks[i] = nullptr; /* just in case */
    }
  }
}

int Module::AddMetadataAttr(const std::string &name, int size,
                            bess::metadata::AccessMode mode) {
  size_t n = num_attrs;
  int ret;

  if (n >= bess::metadata::kMaxAttrsPerModule)
    return -ENOSPC;

  if (!is_valid_attr(name.c_str(), size, mode))
    return -EINVAL;

  attrs[n].name = name;
  attrs[n].size = size;
  attrs[n].mode = mode;
  attrs[n].scope_id = -1;
  if ((ret = pipeline_->RegisterAttribute(&attrs[n]))) {
    return ret;
  }

  num_attrs++;

  return n;
}

int Module::GrowGates(struct gates *gates, gate_idx_t gate) {
  struct gate **new_arr;
  gate_idx_t old_size;
  gate_idx_t new_size;

  new_size = gates->curr_size ?: 1;

  while (new_size <= gate)
    new_size *= 2;

  if (new_size > MAX_GATES)
    new_size = MAX_GATES;

  new_arr =
      (struct gate **)mem_realloc(gates->arr, sizeof(struct gate *) * new_size);
  if (!new_arr)
    return -ENOMEM;

  gates->arr = new_arr;

  old_size = gates->curr_size;
  gates->curr_size = new_size;

  /* initialize the newly created gates */
  memset(&gates->arr[old_size], 0,
         sizeof(struct gate *) * (new_size - old_size));

  return 0;
}

/* returns -errno if fails */
int Module::ConnectModules(gate_idx_t ogate_idx, Module *m_next,
                           gate_idx_t igate_idx) {
  struct gate *ogate;
  struct gate *igate;

  if (ogate_idx >= module_builder_->NumOGates() || ogate_idx >= MAX_GATES)
    return -EINVAL;

  if (igate_idx >= m_next->module_builder()->NumIGates() ||
      igate_idx >= MAX_GATES)
    return -EINVAL;

  if (ogate_idx >= ogates.curr_size) {
    int ret = GrowGates(&ogates, ogate_idx);
    if (ret)
      return ret;
  }

  /* already being used? */
  if (is_active_gate(&ogates, ogate_idx))
    return -EBUSY;

  if (igate_idx >= m_next->igates.curr_size) {
    int ret = m_next->GrowGates(&m_next->igates, igate_idx);
    if (ret)
      return ret;
  }

  ogate = (struct gate *)mem_alloc(sizeof(struct gate));
  if (!ogate)
    return -ENOMEM;

  ogates.arr[ogate_idx] = ogate;

  igate = m_next->igates.arr[igate_idx];
  if (!igate) {
    igate = (struct gate *)mem_alloc(sizeof(struct gate));
    if (!igate) {
      mem_free(ogate);
      return -ENOMEM;
    }

    m_next->igates.arr[igate_idx] = igate;

    igate->m = m_next;
    igate->gate_idx = igate_idx;
    igate->arg = m_next;
    cdlist_head_init(&igate->in.ogates_upstream);
  }

  ogate->m = this;
  ogate->gate_idx = ogate_idx;
  ogate->arg = m_next;
  ogate->out.igate = igate;
  ogate->out.igate_idx = igate_idx;

  cdlist_add_tail(&igate->in.ogates_upstream, &ogate->out.igate_upstream);

  return 0;
}

int Module::DisconnectModules(gate_idx_t ogate_idx) {
  struct gate *ogate;
  struct gate *igate;

  if (ogate_idx >= module_builder_->NumOGates())
    return -EINVAL;

  /* no error even if the ogate is unconnected already */
  if (!is_active_gate(&ogates, ogate_idx))
    return 0;

  ogate = ogates.arr[ogate_idx];
  if (!ogate)
    return 0;

  igate = ogate->out.igate;

  /* Does the igate become inactive as well? */
  cdlist_del(&ogate->out.igate_upstream);
  if (cdlist_is_empty(&igate->in.ogates_upstream)) {
    Module *m_next = igate->m;
    m_next->igates.arr[igate->gate_idx] = nullptr;
    mem_free(igate);
  }

  ogates.arr[ogate_idx] = nullptr;
  mem_free(ogate);

  return 0;
}

int Module::DisconnectModulesUpstream(gate_idx_t igate_idx) {
  struct gate *igate;
  struct gate *ogate;
  struct gate *ogate_next;

  if (igate_idx >= module_builder_->NumIGates())
    return -EINVAL;

  /* no error even if the igate is unconnected already */
  if (!is_active_gate(&igates, igate_idx))
    return 0;

  igate = igates.arr[igate_idx];
  if (!igate)
    return 0;

  cdlist_for_each_entry_safe(ogate, ogate_next, &igate->in.ogates_upstream,
                             out.igate_upstream) {
    Module *m_prev = ogate->m;
    m_prev->ogates.arr[ogate->gate_idx] = nullptr;
    mem_free(ogate);
  }

  igates.arr[igate_idx] = nullptr;
  mem_free(igate);

  return 0;
}

void Module::RunSplit(const gate_idx_t *out_gates,
                      struct pkt_batch *mixed_batch) {
  int cnt = mixed_batch->cnt;
  int num_pending = 0;

  snb_array_t p_pkt = &mixed_batch->pkts[0];

  gate_idx_t pending[MAX_PKT_BURST];
  struct pkt_batch batches[MAX_PKT_BURST];

  struct pkt_batch *splits = ctx.splits();

  /* phase 1: collect unique ogates into pending[] */
  for (int i = 0; i < cnt; i++) {
    struct pkt_batch *batch;
    gate_idx_t ogate;

    ogate = out_gates[i];
    batch = &splits[ogate];

    batch_add(batch, *(p_pkt++));

    pending[num_pending] = ogate;
    num_pending += (batch->cnt == 1);
  }

  /* phase 2: move batches to local stack, since it may be reentrant */
  for (int i = 0; i < num_pending; i++) {
    struct pkt_batch *batch;

    batch = &splits[pending[i]];
    batch_copy(&batches[i], batch);
    batch_clear(batch);
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

  assert(s->depth == 0);
  assert(s->buflen == 0);

  s->buflen = snprintf(s->buf + s->buflen, MAX_TRACE_BUFSIZE - s->buflen,
                       "Worker %d %-8s | %s", current_wid, type, mod->name());

  s->curr_indent = s->buflen;
}

void _trace_end(int print_out) {
  struct callstack *s = &worker_callstack;

  assert(s->depth == 0);
  s->buflen = 0;
  s->newlined = 0;

  if (print_out) {
    DLOG(INFO) << s->buf;
  }
}

void _trace_before_call(Module *mod, Module *next, struct pkt_batch *batch) {
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
  assert(s->depth < MAX_TRACE_DEPTH);

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

#if TCPDUMP_GATES
int Module::EnableTcpDump(const char *fifo, gate_idx_t ogate) {
  static const struct pcap_hdr PCAP_FILE_HDR = {
      .magic_number = PCAP_MAGIC_NUMBER,
      .version_major = PCAP_VERSION_MAJOR,
      .version_minor = PCAP_VERSION_MINOR,
      .thiszone = PCAP_THISZONE,
      .sigfigs = PCAP_SIGFIGS,
      .snaplen = PCAP_SNAPLEN,
      .network = PCAP_NETWORK,
  };

  int fd;
  int ret;

  /* Don't allow tcpdump to be attached to gates that are not active */
  if (!is_active_gate(&ogates, ogate))
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

  ogates.arr[ogate]->fifo_fd = fd;
  ogates.arr[ogate]->tcpdump = 1;

  return 0;
}

int Module::DisableTcpDump(gate_idx_t ogate) {
  if (!is_active_gate(&ogates, ogate))
    return -EINVAL;

  if (!ogates.arr[ogate]->tcpdump)
    return -EINVAL;

  ogates.arr[ogate]->tcpdump = 0;
  close(ogates.arr[ogate]->fifo_fd);

  return 0;
}

void Module::DumpPcapPkts(struct gate *gate, struct pkt_batch *batch) {
  struct timeval tv;

  int ret = 0;
  int fd = gate->fifo_fd;

  gettimeofday(&tv, nullptr);

  for (int i = 0; i < batch->cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];
    struct pcap_rec_hdr rec = {
        .ts_sec = (uint32_t)tv.tv_sec,
        .ts_usec = (uint32_t)tv.tv_usec,
        .incl_len = pkt->mbuf.data_len,
        .orig_len = pkt->mbuf.pkt_len,
    };

    struct iovec vec[2] = {{&rec, sizeof(rec)},
                           {snb_head_data(pkt), (size_t)snb_head_len(pkt)}};

    ret = writev(fd, vec, 2);
    if (ret < 0) {
      if (errno == EPIPE) {
        DLOG(WARNING) << "Broken pipe: stopping tcpdump";
        gate->tcpdump = 0;
        gate->fifo_fd = 0;
        close(fd);
      }
      return;
    }
  }
}
#endif
