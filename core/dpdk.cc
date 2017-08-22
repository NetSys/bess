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

#include "dpdk.h"

#include <syslog.h>
#include <unistd.h>

#include <glog/logging.h>
#include <rte_config.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "utils/time.h"
#include "worker.h"

static int get_numa_count() {
  FILE *fp;

  int matched;
  int cnt;

  fp = fopen("/sys/devices/system/node/possible", "r");
  if (!fp) {
    goto fail;
  }

  matched = fscanf(fp, "0-%d", &cnt);
  if (matched == 1) {
    return cnt + 1;
  }

fail:
  if (fp) {
    fclose(fp);
  }

  LOG(INFO) << "/sys/devices/system/node/possible not available. "
            << "Assuming a single-node system...";
  return 1;
}

static void disable_syslog() {
  setlogmask(0x01);
}

static void enable_syslog() {
  setlogmask(0xff);
}

/* for log messages during rte_eal_init() */
static ssize_t dpdk_log_init_writer(void *, const char *data, size_t len) {
  enable_syslog();
  LOG(INFO) << std::string(data, len);
  disable_syslog();
  return len;
}

static ssize_t dpdk_log_writer(void *, const char *data, size_t len) {
  LOG(INFO) << std::string(data, len);
  return len;
}

class CmdLineOpts {
 public:
  explicit CmdLineOpts(std::initializer_list<std::string> args)
      : args_(), argv_({nullptr}) {
    Append(args);
  }

  void Append(std::initializer_list<std::string> args) {
    for (const std::string &arg : args) {
      args_.emplace_back(arg.begin(), arg.end());
      args_.back().push_back('\0');
      argv_.insert(argv_.begin() + argv_.size() - 1, args_.back().data());
    }
  }

  char **Argv() { return argv_.data(); }

  int Argc() const { return args_.size(); }

 private:
  // Contains a copy of each argument.
  std::vector<std::vector<char>> args_;
  // Pointer to each argument (in `args_`), plus an extra `nullptr`.
  std::vector<char *> argv_;
};

static void init_eal(const char *prog_name, int mb_per_socket,
                     int multi_instance, bool no_huge, int default_core) {
  int numa_count = get_numa_count();

  CmdLineOpts rte_args{
      prog_name, "--master-lcore", std::to_string(RTE_MAX_LCORE - 1), "--lcore",
      std::to_string(RTE_MAX_LCORE - 1) + "@" + std::to_string(default_core),
      // Do not bother with /var/run/.rte_config and .rte_hugepage_info,
      // since we don't want to interfere with other DPDK applications.
      "--no-shconf",
  };

  if (no_huge) {
    rte_args.Append({"--no-huge"});
    rte_args.Append({"-m", std::to_string(mb_per_socket)});
  } else {
    std::string opt_socket_mem = std::to_string(mb_per_socket);
    for (int i = 1; i < numa_count; i++) {
      opt_socket_mem += "," + std::to_string(mb_per_socket);
    }

    rte_args.Append({"--socket-mem", opt_socket_mem});

    // Unlink mapped hugepage files so that memory can be reclaimed as soon as
    // bessd terminates.
    rte_args.Append({"--huge-unlink"});
  }

  if (!no_huge && multi_instance) {
    rte_args.Append({"--file-prefix", "rte" + std::to_string(getpid())});
  }

  /* reset getopt() */
  optind = 0;

  /* DPDK creates duplicated outputs (stdout and syslog).
   * We temporarily disable syslog, then set our log handler */
  cookie_io_functions_t dpdk_log_init_funcs;
  cookie_io_functions_t dpdk_log_funcs;

  std::memset(&dpdk_log_init_funcs, 0, sizeof(dpdk_log_init_funcs));
  std::memset(&dpdk_log_funcs, 0, sizeof(dpdk_log_funcs));

  dpdk_log_init_funcs.write = &dpdk_log_init_writer;
  dpdk_log_funcs.write = &dpdk_log_writer;

  FILE *org_stdout = stdout;
  stdout = fopencookie(nullptr, "w", dpdk_log_init_funcs);

  disable_syslog();
  int ret = rte_eal_init(rte_args.Argc(), rte_args.Argv());
  if (ret < 0) {
    LOG(ERROR) << "rte_eal_init() failed: ret = " << ret;
    exit(EXIT_FAILURE);
  }

  enable_syslog();
  fclose(stdout);
  stdout = org_stdout;

  rte_openlog_stream(fopencookie(nullptr, "w", dpdk_log_funcs));
}

// Returns the last core ID of all cores, as the default core all threads will
// run on. If the process was run with a limited set of cores (by `taskset`),
// the last one among them will be picked.
static int determine_default_core() {
  cpu_set_t set;

  int ret = pthread_getaffinity_np(pthread_self(), sizeof(set), &set);
  if (ret < 0) {
    PLOG(WARNING) << "pthread_getaffinity_np()";
    return 0;  // Core 0 as a fallback
  }

  // Choose the last core available
  for (int i = CPU_SETSIZE; i >= 0; i--) {
    if (CPU_ISSET(i, &set)) {
      return i;
    }
  }

  // This will never happen, but just in case.
  PLOG(WARNING) << "No core is allowed for the process?";
  return 0;
}

void init_dpdk(const ::std::string &prog_name, int mb_per_socket,
               int multi_instance, bool no_huge) {
  // Isolate all background threads in a separate core.
  // All non-worker threads will be scheduled on default_core,
  // including threads spawned by DPDK and gRPC.
  // FIXME: This is a temporary fix. If a new worker thread is allocated on the
  //        same core, background threads should migrate to another core.
  int default_core = determine_default_core();
  ctx.SetNonWorker();

  init_eal(prog_name.c_str(), mb_per_socket, multi_instance, no_huge,
           default_core);
}
