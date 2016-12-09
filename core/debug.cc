#include "debug.h"

#include <execinfo.h>
#include <gnu/libc-version.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <glog/logging.h>
#include <rte_config.h>
#include <rte_version.h>

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stack>
#include <string>

#include "module.h"
#include "packet.h"
#include "scheduler.h"
#include "traffic_class.h"
#include "utils/htable.h"

namespace bess {
namespace debug {

static const char *si_code_to_str(int sig_num, int si_code) {
  /* See the manpage of sigaction() */
  switch (si_code) {
    case SI_USER:
      return "SI_USER: kill";
    case SI_KERNEL:
      return "SI_KERNEL: sent by the kernel";
    case SI_QUEUE:
      return "SI_QUEUE: sigqueue";
    case SI_TIMER:
      return "SI_TIMER: POSIX timer expired";
    case SI_MESGQ:
      return "SI_MESGQ: POSIX message queue state changed";
    case SI_ASYNCIO:
      return "SI_ASYNCIO: AIO completed";
    case SI_SIGIO:
      return "SI_SIGIO: Queued SIGIO";
    case SI_TKILL:
      return "SI_TKILL: tkill or tgkill";
  }

  switch (sig_num) {
    case SIGILL:
      switch (si_code) {
        case ILL_ILLOPC:
          return "ILL_ILLOPC: illegal opcode";
        case ILL_ILLOPN:
          return "ILL_ILLOPN: illegal operand";
        case ILL_ILLADR:
          return "ILL_ILLADR: illegal addressing mode";
        case ILL_ILLTRP:
          return "ILL_ILLTRP: illegal trap";
        case ILL_PRVOPC:
          return "ILL_PRVOPC: privileged opcode";
        case ILL_PRVREG:
          return "ILL_PRVREG: privileged register";
        case ILL_COPROC:
          return "ILL_COPROC: coprocessor error";
        case ILL_BADSTK:
          return "ILL_PRVREG: internal stack error";
        default:
          return "unknown";
      }

    case SIGFPE:
      switch (si_code) {
        case FPE_INTDIV:
          return "FPE_INTDIV: integer divide by zero";
        case FPE_INTOVF:
          return "FPE_INTOVF: integer overflow";
        case FPE_FLTDIV:
          return "FPE_FLTDIV: floating-point divide by zero";
        case FPE_FLTOVF:
          return "FPE_FLTOVF: floating-point overflow";
        case FPE_FLTUND:
          return "FPE_FLTOVF: floating-point underflow";
        case FPE_FLTRES:
          return "FPE_FLTOVF: floating-point inexact result";
        case FPE_FLTINV:
          return "FPE_FLTOVF: floating-point invalid operation";
        case FPE_FLTSUB:
          return "FPE_FLTOVF: subscript out of range";
        default:
          return "unknown";
      }

    case SIGSEGV:
      switch (si_code) {
        case SEGV_MAPERR:
          return "SEGV_MAPERR: address not mapped to object";
        case SEGV_ACCERR:
          return "SEGV_ACCERR: invalid permissions for mapped object";
        default:
          return "unknown";
      }

    case SIGBUS:
      switch (si_code) {
        case BUS_ADRALN:
          return "BUS_ADRALN: invalid address alignment";
        case BUS_ADRERR:
          return "BUS_ADRERR: nonexistent physical address";
        case BUS_OBJERR:
          return "BUS_OBJERR: object-specific hardware error";
#if 0
        case BUS_MCEERR_AR:
          return "BUS_MCEERR_AR: Hardware memory error consumed on a machine "
                 "check";
        case BUS_MCEERR_AO:
          return "BUS_MCEERR_AO: Hardware memory error detected in process but "
                 "not consumed";
#endif
        default:
          return "unknown";
      }
  }

  return "si_code unavailable for unknown signal";
}

/* addr2line must be available.
 * prints out the code lines [lineno - context, lineno + context] */
static std::string print_code(char *symbol, int context) {
  std::ostringstream ret;
  char executable[1024];
  char addr[1024];

  char cmd[1024];

  FILE *proc;

  /* Symbol examples:
   * ./bessd(run_worker+0x8e) [0x419d0e]" */
  /* ./bessd() [0x4149d8] */
  sscanf(symbol, "%[^(](%*s [%[^]]]", executable, addr);

  snprintf(cmd, sizeof(cmd), "addr2line -C -i -f -p -e %s %s 2> /dev/null",
           executable, addr);

  proc = popen(cmd, "r");
  if (!proc) {
    return "";
  }

  for (;;) {
    char line[1024];
    char *p;

    char *filename;
    int lineno;
    int curr;

    FILE *fp;

    p = fgets(line, sizeof(line), proc);
    if (!p) {
      break;
    }

    if (line[strlen(line) - 1] != '\n') {
      // no LF found (line is too long?)
      continue;
    }

    // addr2line examples:
    // sched_free at /home/sangjin/.../tc.c:277 (discriminator 2)
    // run_worker at /home/sangjin/bess/core/module.c:653

    line[strlen(line) - 1] = '\0';

    ret << "    " << line << std::endl;

    if (line[strlen(line) - 1] == ')') {
      *(strstr(line, " (discriminator")) = '\0';
    }

    p = strrchr(p, ' ');
    if (!p) {
      continue;
    }

    p++;  // now p points to the last token (filename)
    char *rest;

    filename = strtok_r(p, ":", &rest);

    if (strcmp(filename, "??") == 0) {
      continue;
    }

    p = strtok_r(nullptr, "", &rest);
    if (!p) {
      continue;
    }

    lineno = atoi(p);

    fp = fopen(filename, "r");
    if (!fp) {
      ret << "        (file/line not available)" << std::endl;
      continue;
    }

    for (curr = 1; !feof(fp); curr++) {
      bool discard = true;

      if (abs(curr - lineno) <= context) {
        ret << "      " << (curr == lineno ? "->" : "  ") << " " << curr
            << ": ";
        discard = false;
      } else if (curr > lineno + context) {
        break;
      }

      while (true) {
        p = fgets(line, sizeof(line), fp);
        if (!p) {
          break;
        }

        if (!discard) {
          ret << line;
        }

        if (line[strlen(line) - 1] != '\n') {
          if (feof(fp)) {
            ret << std::endl;
          }
        } else {
          break;
        }
      }
    }

    fclose(fp);
  }

  pclose(proc);

  return ret.str();
}

static void *trap_ip;
static std::string oops_msg;

static std::string DumpStack() {
  const size_t max_stack_depth = 64;
  void *addrs[max_stack_depth];

  std::ostringstream stack;

  char **symbols;
  int skips;

  /* the linker requires -rdynamic for non-exported symbols */
  int cnt = backtrace(addrs, max_stack_depth);
  if (cnt < 3) {
    stack << "ERROR: backtrace() failed" << std::endl;
    return stack.str();
  }

  /* addrs[0]: this function
   * addrs[1]: sigaction in glibc
   * addrs[2]: the trigerring instruction pointer *or* its caller
   *           (depending on the kernel behavior?) */
  if (addrs[2] == trap_ip) {
    skips = 2;
  } else {
    if (trap_ip != nullptr)
      addrs[1] = trap_ip;
    skips = 1;
  }

  // The return addresses point to the next instruction after its call,
  // so adjust them by -1
  for (int i = skips + 1; i < cnt; i++)
    addrs[i] = reinterpret_cast<void *>((uintptr_t)addrs[i] - 1);

  symbols = backtrace_symbols(addrs, cnt);

  if (symbols) {
    stack << "Backtrace (recent calls first) ---" << std::endl;

    for (int i = skips; i < cnt; ++i) {
      stack << "(" << i - skips << "): " << symbols[i] << std::endl;
      stack << print_code(symbols[i], (i == skips) ? 3 : 0);
    }

    free(symbols);  // required by backtrace_symbols()
  } else {
    stack << "ERROR: backtrace_symbols() failed\n";
  }

  return stack.str();
}

[[noreturn]] static void exit_failure() {
  exit(EXIT_FAILURE);
}

[[noreturn]] void GoPanic() {
  if (oops_msg == "")
    oops_msg = DumpStack();

  // Create a crash log file
  try {
    std::ofstream fp(P_tmpdir "/bessd_crash.log");
    fp << oops_msg;
    fp.close();
  } catch (...) {
    // Ignore any errors.
  }

  // The default failure function of glog calls abort(), which causes SIGABRT
  google::InstallFailureFunction(exit_failure);
  LOG(FATAL) << oops_msg;
}

// SIGUSR1 is used to examine the current callstack, without aborting.
// (useful when the process seems stuck)
// TODO: Only use async-signal-safe operations in the signal handler.
static void TrapHandler(int sig_num, siginfo_t *info, void *ucontext) {
  std::ostringstream oops;
  struct ucontext *uc;
  bool is_fatal = (sig_num != SIGUSR1);
  static volatile bool already_trapped = false;

  // avoid recursive traps
  if (is_fatal && !__sync_bool_compare_and_swap(&already_trapped, false, true))
    return;

  uc = (struct ucontext *)ucontext;

#if __i386
  trap_ip = reinterpret_cast<void *>(uc->uc_mcontext.gregs[REG_EIP]);
#elif __x86_64
  trap_ip = reinterpret_cast<void *>(uc->uc_mcontext.gregs[REG_RIP]);
#else
#error neither x86 or x86-64
#endif

  if (is_fatal) {
    oops << "A critical error has occured. Aborting..." << std::endl;
  }

  oops << "Signal: " << sig_num << " (" << strsignal(sig_num)
       << "), si_code: " << info->si_code << " ("
       << si_code_to_str(sig_num, info->si_code) << ")" << std::endl;

  oops << "pid: " << getpid() << ", tid: " << (pid_t)syscall(SYS_gettid)
       << ", address: " << info->si_addr << ", IP: " << trap_ip << std::endl;

  if (is_fatal) {
    oops << DumpStack();
    oops_msg = oops.str();
    GoPanic();
    // Never reaches here. LOG(FATAL) will terminate the process.
  } else {
    LOG(INFO) << oops.str() << DumpStack();
  }
}

void SetTrapHandler() {
  const int signals[] = {
      SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT,
      // SIGUSR1 is special in that it is triggered by user and does not abort
      SIGUSR1,
  };

  const int ignored_signals[] = {
      SIGPIPE,
  };

  struct sigaction sigact;
  size_t i;

  unlink(P_tmpdir "/bessd_crash.log");

  sigact.sa_sigaction = TrapHandler;
  sigact.sa_flags = SA_RESTART | SA_SIGINFO;

  for (i = 0; i < sizeof(signals) / sizeof(int); i++) {
    int ret = sigaction(signals[i], &sigact, nullptr);
    DCHECK_NE(ret, 1);
  }

  for (i = 0; i < sizeof(ignored_signals) / sizeof(int); i++) {
    signal(ignored_signals[i], SIG_IGN);
  }
}

void DumpTypes(void) {
  printf("gcc: %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
  printf("glibc: %s-%s\n", gnu_get_libc_version(), gnu_get_libc_release());
  printf("DPDK: %s\n", rte_version());

  printf("sizeof(char)=%zu\n", sizeof(char));
  printf("sizeof(short)=%zu\n", sizeof(short));
  printf("sizeof(int)=%zu\n", sizeof(int));
  printf("sizeof(long)=%zu\n", sizeof(long));
  printf("sizeof(long long)=%zu\n", sizeof(long long));
  printf("sizeof(intmax_t)=%zu\n", sizeof(intmax_t));
  printf("sizeof(void *)=%zu\n", sizeof(void *));
  printf("sizeof(size_t)=%zu\n", sizeof(size_t));

  printf("sizeof(HTableBase)=%zu\n", sizeof(HTableBase));

  printf("sizeof(rte_mbuf)=%zu\n", sizeof(struct rte_mbuf));
  printf("sizeof(Packet)=%zu\n", sizeof(Packet));
  printf("sizeof(pkt_batch)=%zu\n", sizeof(bess::PacketBatch));
  printf("sizeof(Scheduler)=%zu sizeof(sched_stats)=%zu\n", sizeof(Scheduler),
         sizeof(struct sched_stats));
  printf("sizeof(TrafficClass)=%zu sizeof(tc_stats)=%zu\n",
         sizeof(TrafficClass), sizeof(struct tc_stats));
  printf("sizeof(Task)=%zu\n", sizeof(Task));

  printf("sizeof(Module)=%zu\n", sizeof(Module));
  printf("sizeof(Gate)=%zu\n", sizeof(bess::Gate));
  printf("sizeof(IGate)=%zu\n", sizeof(bess::IGate));
  printf("sizeof(OGate)=%zu\n", sizeof(bess::OGate));

  printf("sizeof(worker_context)=%zu\n", sizeof(Worker));
}

}  // namespace debug
}  // namespace bess
