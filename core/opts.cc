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

#include "opts.h"

#include <glog/logging.h>

#include <cstdint>

#include "bessd.h"
#include "worker.h"

// Port this BESS instance listens on.
// Panda came up with this default number
static const int kDefaultPort = 0x02912;  // 10514 in decimal
static const char *kDefaultBindAddr = "127.0.0.1";

// TODO(barath): Rename these flags to something more intuitive.
DEFINE_bool(t, false, "Dump the size of internal data structures");
DEFINE_string(i, "/var/run/bessd.pid", "Specifies where to write the pidfile");
DEFINE_bool(f, false, "Run BESS in foreground mode (for developers)");
DEFINE_bool(k, false, "Kill existing BESS instance, if any");
DEFINE_bool(s, false, "Show TC statistics every second");
DEFINE_bool(d, false, "Run BESS in debug mode (with debug log messages)");
DEFINE_bool(skip_root_check, false,
            "Skip checking that the process is running as root.");
DEFINE_string(modules, bess::bessd::GetCurrentDirectory() + "modules",
              "Load modules from the specified directory");
DEFINE_bool(core_dump, false, "Generate a core dump on fatal faults");
DEFINE_bool(no_crashlog, false, "Disable the generation of a crash log file");

// Note: currently BESS-managed hugepages do not support VFIO driver,
//       so DPDK is default for now.
DEFINE_bool(dpdk, true, "Let DPDK manage hugepages");

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
DEFINE_string(grpc_url, "",
              "Specifies the URL where the BESS gRPC server should listen. "
              "If non empty, overrides -b and -p options.");
DEFINE_string(b, kDefaultBindAddr,
              "Specifies the IP address of the interface the BESS gRPC server "
              "should bind to, if --grpc_url is empty. Deprecated, please use"
              "--grpc_url instead");
DEFINE_int32(
    p, kDefaultPort,
    "Specifies the TCP port on which BESS listens for controller connections, "
    "if --grpc_url is empty. Deprecated, please use --grpc_url instead");
static const bool _p_dummy[[maybe_unused]] =
    google::RegisterFlagValidator(&FLAGS_p, &ValidateTCPPort);

static bool ValidateMegabytesPerSocket(const char *, int32_t value) {
  if (value < 0) {
    LOG(ERROR) << "Invalid memory size: " << value;
    return false;
  }

  return true;
}
DEFINE_int32(m, 1024,
             "Specifies per-socket hugepages to allocate (in MBs). "
             "If set to 0, no hugepage is used");
static const bool _m_dummy[[maybe_unused]] =
    google::RegisterFlagValidator(&FLAGS_m, &ValidateMegabytesPerSocket);

static bool ValidateBuffersPerSocket(const char *, int32_t value) {
  if (value <= 0) {
    LOG(ERROR) << "Invalid number of buffers: " << value;
    return false;
  }
  if (value & (value - 1)) {
    LOG(ERROR) << "Number of buffers must be a power of 2: " << value;
    return false;
  }
  return true;
}
DEFINE_int32(buffers, 262144,
             "Specifies how many packet buffers to allocate per socket,"
             " must be a power of 2.");
static const bool _buffers_dummy[[maybe_unused]] =
    google::RegisterFlagValidator(&FLAGS_buffers, &ValidateBuffersPerSocket);
