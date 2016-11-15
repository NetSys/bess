#include <rte_launch.h>

#include <glog/logging.h>

#include "bessd.h"
#include "debug.h"
#include "dpdk.h"
#include "master.h"
#include "opts.h"
#include "port.h"
#include "snbuf.h"

int main(int argc, char *argv[]) {
  FLAGS_logbuflevel = -1;
  FLAGS_colorlogtostderr = true;
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureFunction(bess::debug::GoPanic);
  bess::debug::SetTrapHandler();

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

  // Store our PID (child's, if daemonized) in the PID file.
  bess::bessd::WritePidfile(pidfile_fd, getpid());

  // TODO(barath): Make these DPDK calls generic, so as to not be so tied to
  // DPDK.
  init_dpdk(argv[0], FLAGS_m, FLAGS_a);
  init_mempool();

  PortBuilder::InitDrivers();

  SetupMaster();

  // Signal the parent that all initialization has been finished.
  if (!FLAGS_f) {
    uint64_t one = 1;
    if (write(signal_fd, &one, sizeof(one)) < 0) {
      PLOG(FATAL) << "write(signal_fd)";
    }
    close(signal_fd);
  }

  RunMaster();

  rte_eal_mp_wait_lcore();
  close_mempool();

  return 0;
}
