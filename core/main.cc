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

#include <rte_launch.h>

#include <glog/logging.h>

#include "bessctl.h"
#include "bessd.h"
#include "debug.h"
#include "dpdk.h"
#include "opts.h"
#include "packet.h"
#include "port.h"
#include "version.h"

int main(int argc, char *argv[]) {
  FLAGS_logbuflevel = -1;
  FLAGS_colorlogtostderr = true;
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureFunction(bess::debug::GoPanic);
  bess::debug::SetTrapHandler();

  google::SetVersionString(VERSION);
  google::SetUsageMessage("BESS Command Line Options:");
  google::ParseCommandLineFlags(&argc, &argv, true);
  bess::bessd::ProcessCommandLineArgs();
  bess::bessd::CheckRunningAsRoot();

  int pidfile_fd = bess::bessd::CheckUniqueInstance(FLAGS_i);
  ignore_result(bess::bessd::SetResourceLimit());

  int signal_fd = -1;
  if (FLAGS_f) {
    LOG(INFO) << "Launching BESS daemon in process mode...";
  } else {
    LOG(INFO) << "Launching BESS daemon in background...";
    signal_fd = bess::bessd::Daemonize();
  }

  LOG(INFO) << "bessd " << google::VersionString();

  // Store our PID (child's, if daemonized) in the PID file.
  bess::bessd::WritePidfile(pidfile_fd, getpid());

  // Load plugins
  if (!bess::bessd::LoadPlugins(FLAGS_modules)) {
    PLOG(FATAL) << "LoadPlugins() failed to load from directory: "
                << FLAGS_modules;
  }

  // TODO(barath): Make these DPDK calls generic, so as to not be so tied to
  // DPDK.
  init_dpdk(argv[0], FLAGS_m, FLAGS_a, FLAGS_no_huge);
  bess::init_mempool();

  PortBuilder::InitDrivers();

  {
    ApiServer server;
    server.Listen(FLAGS_b, FLAGS_p);

    // Signal the parent that all initialization has been finished.
    if (!FLAGS_f) {
      uint64_t one = 1;
      if (write(signal_fd, &one, sizeof(one)) < 0) {
        PLOG(FATAL) << "write(signal_fd)";
      }
      close(signal_fd);
    }

    server.Run();
  }

  rte_eal_mp_wait_lcore();
  bess::close_mempool();

  LOG(INFO) << "BESS daemon has been gracefully shut down";

  return 0;
}
