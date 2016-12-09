#include "opts.h"

#include <glog/logging.h>

#include <cstdint>

#include "worker.h"

// Port this BESS instance listens on.
// Panda came up with this default number
static const int kDefaultPort = 0x02912;  // 10514 in decimal

// TODO(barath): Rename these flags to something more intuitive.
DEFINE_bool(t, false, "Dump the size of internal data structures");
DEFINE_string(i, "/var/run/bessd.pid", "Specifies where to write the pidfile");
DEFINE_bool(f, false, "Run BESS in foreground mode (for developers)");
DEFINE_bool(k, false, "Kill existing BESS instance, if any");
DEFINE_bool(s, false, "Show TC statistics every second");
DEFINE_bool(d, false, "Run BESS in debug mode (with debug log messages)");
DEFINE_bool(a, false, "Allow multiple instances");
DEFINE_bool(no_huge, false, "Disable hugepages");

static bool ValidateCoreID(const char *, int32_t value) {
  if (!is_cpu_present(value)) {
    LOG(ERROR) << "Invalid core ID: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(c, 0, "Core ID for the default worker thread");
static const bool _c_dummy[[maybe_unused]] =
    google::RegisterFlagValidator(&FLAGS_c, &ValidateCoreID);

static bool ValidateTCPPort(const char *, int32_t value) {
  if (value <= 0) {
    LOG(ERROR) << "Invalid TCP port number: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(
    p, kDefaultPort,
    "Specifies the TCP port on which BESS listens for controller connections");
static const bool _p_dummy[[maybe_unused]] =
    google::RegisterFlagValidator(&FLAGS_p, &ValidateTCPPort);

static bool ValidateMegabytesPerSocket(const char *, int32_t value) {
  if (value <= 0) {
    LOG(ERROR) << "Invalid memory size: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(m, 2048, "Specifies how many megabytes to use per socket");
static const bool _m_dummy[[maybe_unused]] =
    google::RegisterFlagValidator(&FLAGS_m, &ValidateMegabytesPerSocket);
