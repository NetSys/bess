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

static void init_eal(const char *prog_name, int mb_per_socket,
                     int multi_instance, bool no_huge, int default_core) {
  int rte_argc = 0;
  const char *rte_argv[16];

  char opt_master_lcore[1024];
  char opt_lcore_bitmap[1024];
  char opt_socket_mem[1024];
  char opt_file_prefix[1024];

  int numa_count = get_numa_count();

  int ret;
  int i;

  FILE *org_stdout;

  snprintf(opt_master_lcore, sizeof(opt_master_lcore), "%d", RTE_MAX_LCORE - 1);
  snprintf(opt_lcore_bitmap, sizeof(opt_lcore_bitmap), "%d@%d",
           RTE_MAX_LCORE - 1, default_core);

  snprintf(opt_socket_mem, sizeof(opt_socket_mem), "%d", mb_per_socket);
  for (i = 1; i < numa_count; i++) {
    auto len = strlen(opt_socket_mem);
    snprintf(opt_socket_mem + len, sizeof(opt_socket_mem) - len, ",%d",
             mb_per_socket);
  }

  rte_argv[rte_argc++] = prog_name;
  rte_argv[rte_argc++] = "--master-lcore";
  rte_argv[rte_argc++] = opt_master_lcore;
  rte_argv[rte_argc++] = "--lcore";
  rte_argv[rte_argc++] = opt_lcore_bitmap;

  // Do not bother with /var/run/.rte_config and .rte_hugepage_info,
  // since we don't want to interfere with other DPDK applications.
  rte_argv[rte_argc++] = "--no-shconf";
  if (no_huge) {
    rte_argv[rte_argc++] = "--no-huge";
  } else {
    rte_argv[rte_argc++] = "--socket-mem";
    rte_argv[rte_argc++] = opt_socket_mem;

    // Unlink mapped hugepage files so that memory can be reclaimed as soon as
    // bessd terminates.
    rte_argv[rte_argc++] = "--huge-unlink";
  }
  if (!no_huge && multi_instance) {
    snprintf(opt_file_prefix, sizeof(opt_file_prefix), "rte%lld",
             (long long)getpid());
    /* Casting to long long isn't guaranteed by POSIX to not lose
     * any information, but should be okay for Linux and BSDs for
     * the foreseeable future. */
    rte_argv[rte_argc++] = "--file-prefix";
    rte_argv[rte_argc++] = opt_file_prefix;
  }
  rte_argv[rte_argc] = nullptr;

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

  org_stdout = stdout;
  stdout = fopencookie(nullptr, "w", dpdk_log_init_funcs);

  disable_syslog();
  ret = rte_eal_init(rte_argc, const_cast<char **>(rte_argv));
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
