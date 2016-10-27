#include <rte_launch.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "bessd.h"
#include "dpdk.h"
#include "master.h"
#include "opts.h"
#include "snbuf.h"
#include "test.h"

int main(int argc, char *argv[]) {
  google::InitGoogleLogging(argv[0]);

  google::SetUsageMessage("BESS Command Line Options:");
  google::ParseCommandLineFlags(&argc, &argv, true);
  bess::bessd::ProcessCommandLineArgs();

  bess::bessd::CheckRunningAsRoot();

  int signal_fd = -1;
  if (FLAGS_f) {
    LOG(INFO) << "Launching BESS daemon in process mode...";
  } else {
    signal_fd = bess::bessd::StartDaemon();
  }

  bess::bessd::CheckUniqueInstance(FLAGS_i);
  bess::bessd::SetResourceLimit();

  // TODO(barath): Make these DPDK calls generic, so as to not be so tied to DPDK.
  init_dpdk(argv[0], FLAGS_m, FLAGS_a);
  init_mempool();

  bess::bessd::InitDrivers();

  SetupMaster();

  // Signal the parent that all initialization has been finished.
  if (!FLAGS_f) {
    uint64_t one = 1;
    if (write(signal_fd, &one, sizeof(one)) < 0) {
      PLOG(FATAL) << "write(signal_fd)";
    }
    close(signal_fd);
  }

  if (FLAGS_g) {
    RunTests();
  } else {
    RunForcedTests();
    RunMaster();
  }

  rte_eal_mp_wait_lcore();
  close_mempool();

  return 0;
}

