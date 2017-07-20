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

#include "debug.h"

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <gnu/libc-version.h>
#include <link.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <rte_config.h>
#include <rte_version.h>

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "module.h"
#include "packet.h"
#include "scheduler.h"
#include "traffic_class.h"
#include "utils/format.h"

namespace bess {
namespace debug {

using bess::utils::Format;
using bess::utils::Parse;

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
#if defined(SEGV_BNDERR)
        case SEGV_BNDERR:
          return "SEGV_BNDERR: failed address bound checks";
#endif
#if defined(SEGV_PKUERR)
        case SEGV_PKUERR:
          return "SEGV_PKUERR: failed protection key checks";
#endif
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
#if defined(BUS_MCEERR_AR)
        case BUS_MCEERR_AR:
          return "BUS_MCEERR_AR: Hardware memory error consumed on a machine "
                 "check";
#endif
#if defined(BUS_MCEERR_AO)
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

//
static std::string FetchLine(std::string filename, int lineno, int context) {
  std::ostringstream ret;
  std::string line;

  std::ifstream f(filename);
  if (!f) {
    return "        (file/line not available)\n";
  }

  for (int curr = 1; std::getline(f, line) && curr <= lineno + context;
       curr++) {
    if (std::abs(curr - lineno) <= context) {
      ret << "      " << (curr == lineno ? "->" : "  ") << " " << curr << ": "
          << line << std::endl;
    }
  }

  return ret.str();
}

// Run an external command and return its standard output.
static std::string RunCommand(std::string cmd) {
  std::ostringstream ret;

  FILE *proc = popen(cmd.c_str(), "r");
  if (proc) {
    while (!feof(proc)) {
      char buf[PIPE_BUF];
      size_t bytes = fread(buf, 1, sizeof(buf), proc);
      ret.write(buf, bytes);
    }
    pclose(proc);
  }

  return ret.str();
}

// If mmap is used (as for shared objects), code address at runtime can be
// arbitrary. This function translates an absolute address into a relative
// address to the object file it belongs to, based on the current memory
// mapping of this process.
static uintptr_t GetRelativeAddress(uintptr_t abs_addr) {
  Dl_info info;
  struct link_map *map;

  if (dladdr1(reinterpret_cast<void *>(abs_addr), &info,
              reinterpret_cast<void **>(&map), RTLD_DL_LINKMAP) != 0) {
    // Normally the base_addr of the executable will be just 0x0
    uintptr_t base_addr = reinterpret_cast<uintptr_t>(map->l_addr);
    return abs_addr - base_addr;
  } else {
    // Error happend. Use the absolute address as a fallback, hoping the address
    // is from the main executable.
    return abs_addr;
  }
}

// addr2line must be available.
// Returns the code lines [lineno - context, lineno + context] */
static std::string PrintCode(std::string symbol, int context) {
  std::ostringstream ret;
  char objfile[PATH_MAX];
  char addr[1024];

  // Symbol examples:
  // ./bessd(run_worker+0x8e) [0x419d0e]
  // ./bessd() [0x4149d8]
  // /home/foo/.../source.so(_ZN6Source7RunTaskEPv+0x55) [0x7f09e912c7b5]
  Parse(symbol, "%[^(](%*s [%[^]]]", objfile, addr);

  uintptr_t sym_addr = std::strtoull(addr, nullptr, 16);
  uintptr_t obj_addr = GetRelativeAddress(sym_addr);

  std::string cmd =
      Format("addr2line -C -i -f -p -e %s 0x%" PRIxPTR " 2> /dev/null", objfile,
             obj_addr);

  std::istringstream result(RunCommand(cmd));
  std::string line;

  while (std::getline(result, line)) {
    // addr2line examples:
    // sched_free at /home/sangjin/.../tc.c:277 (discriminator 2)
    // run_worker at /home/sangjin/bess/core/module.c:653

    ret << "    " << line << std::endl;

    auto pos = line.find(" at ");
    if (pos == std::string::npos) {
      // failed to parse the line
      continue;
    }

    // Remove unnecessary characters (up to " at ", including itself)
    line.erase(0, pos + 4);

    char filename[PATH_MAX];
    int lineno;

    if (Parse(line, "%[^:]:%d", filename, &lineno) == 2) {
      if (std::string(filename) != "??" && lineno != 0) {
        ret << FetchLine(filename, lineno, context);
      }
    }
  }

  return ret.str();
}

static void *trap_ip;
static std::string oops_msg;

static bool SkipSymbol(char *symbol) {
  static const char *blacklist[] = {"(_ZN6google10LogMessage",
                                    "(_ZN6google15LogMessageFatal"};

  for (auto prefix : blacklist) {
    if (strstr(symbol, prefix)) {
      return true;
    }
  }

  return false;
}

[[gnu::noinline]] std::string DumpStack() {
  const size_t max_stack_depth = 64;
  void *addrs[max_stack_depth] = {};

  std::ostringstream stack;

  char **symbols;
  int skips = 0;

  // the linker requires -rdynamic for non-exported symbols
  int cnt = backtrace(addrs, max_stack_depth);

  // in some cases the bottom of the stack is NULL - not useful at all, remove.
  while (cnt > 0 && !addrs[cnt - 1]) {
    cnt--;
  }

  // The return addresses point to the next instruction after its call,
  // so adjust them by -1
  for (int i = 0; i < cnt; i++) {
    if (addrs[i] != trap_ip) {
      addrs[i] =
          reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(addrs[i]) - 1);
    }
  }

  symbols = backtrace_symbols(addrs, cnt);
  if (!symbols) {
    return "ERROR: backtrace_symbols() failed\n";
  }

  // Triggered by a signal? (trap_ip is set)
  if (trap_ip) {
    // addrs[0]: DumpStack() <- this function calling backtrace()
    // addrs[1]: TrapHandler()
    // addrs[2]: sigaction in glibc, or pthread signal handler
    // addrs[3]: the trigerring instruction pointer *or* its caller
    //           (depending on the kernel behavior?)
    if (addrs[3] == trap_ip) {
      skips = 3;
    } else {
      skips = 2;
      addrs[2] = trap_ip;
    }
  } else {
    // LOG(FATAL) or CHECK() failed
    // addrs[0]: DumpStack() <- this function calling backtrace()
    // addrs[1]: GoPanic()
    // addrs[2]: caller of GoPanic()
    skips = 2;
    while (skips < cnt && SkipSymbol(symbols[skips])) {
      skips++;
    }
  }

  stack << "Backtrace (recent calls first) ---" << std::endl;

  for (int i = skips; i < cnt; i++) {
    stack << "(" << i - skips << "): " << symbols[i] << std::endl;
    stack << PrintCode(symbols[i], (i == skips) ? 3 : 0);
  }

  free(symbols);  // required by backtrace_symbols()

  return stack.str();
}

[[noreturn]] static void exit_failure() {
  _exit(EXIT_FAILURE);
}

[[ gnu::noinline, noreturn ]] void GoPanic() {
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
    trap_ip = nullptr;
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

template <typename T>
static void DumpType() {
  std::string type_name = typeid(T).name();
  char *demangled;
  int ret;

  demangled = abi::__cxa_demangle(type_name.c_str(), nullptr, nullptr, &ret);
  if (ret == 0) {
    type_name = demangled;
    std::free(demangled);
  } else {
    DCHECK_EQ(ret, 0);
  }

  std::cout << Format("%-24s %8zu %8zu", type_name.c_str(), sizeof(T),
                      alignof(T))
            << std::endl;
}

void DumpTypes(void) {
  std::cout << "bessd " << google::VersionString() << std::endl;
  std::cout << Format("gcc %d.%d.%d", __GNUC__, __GNUC_MINOR__,
                      __GNUC_PATCHLEVEL__)
            << std::endl;
  std::cout << Format("glibc %s-%s", gnu_get_libc_version(),
                      gnu_get_libc_release())
            << std::endl;
#if defined(__GLIBCXX__)
  std::cout << "libstdc++ " << __GLIBCXX__ << std::endl;
#elif defined(__GLIBCPP__)
  std::cout << "libstdc++ " << __GLIBCPP__ << std::endl;
#else
  std::cout << "libstdc++ ?" << std::endl;
#endif
  std::cout << rte_version() << std::endl;

  std::cout << Format("%-24s %8s %8s", "", "sizeof", "alignof") << std::endl;

  // basic types
  DumpType<char>();
  DumpType<short>();
  DumpType<int>();
  DumpType<long>();
  DumpType<long long>();
  DumpType<intmax_t>();
  DumpType<void *>();
  DumpType<size_t>();
  DumpType<max_align_t>();

  // BESS types
  DumpType<rte_mbuf>();
  DumpType<Packet>();
  DumpType<bess::PacketBatch>();

  DumpType<Scheduler<Task>>();
  DumpType<TrafficClass>();
  DumpType<Task>();

  DumpType<Module>();
  DumpType<bess::Gate>();
  DumpType<bess::IGate>();
  DumpType<bess::OGate>();

  DumpType<Worker>();
}

}  // namespace debug
}  // namespace bess
