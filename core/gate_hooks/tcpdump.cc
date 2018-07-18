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

#include "tcpdump.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <glog/logging.h>

#include "../message.h"
#include "../utils/common.h"
#include "../utils/pcap.h"
#include "../utils/time.h"

const std::string Tcpdump::kName = "TcpDump";

bool TcpdumpOpener::InitFifo(int fd) {
  static const struct pcap_hdr hdr = {
      .magic_number = PCAP_MAGIC_NUMBER,
      .version_major = PCAP_VERSION_MAJOR,
      .version_minor = PCAP_VERSION_MINOR,
      .thiszone = PCAP_THISZONE,
      .sigfigs = PCAP_SIGFIGS,
      .snaplen = PCAP_SNAPLEN,
      .network = PCAP_NETWORK,
  };
  return write(fd, &hdr, sizeof(hdr)) == sizeof(hdr);
}

CommandResponse Tcpdump::Init(const bess::Gate *,
                              const bess::pb::TcpdumpArg &arg) {
  int ret = opener_.Init(arg.fifo(), arg.reconnect());
  if (ret < 0) {
    return CommandFailure(-errno, "inappropriate reinitialization");
  }
  ret = arg.defer() ? opener_.OpenInThread() : opener_.OpenNow();
  if (ret < 0) {
    return CommandFailure(-errno, "Failed to open FIFO");
  }

  return CommandSuccess();
}

void Tcpdump::ProcessBatch(const bess::PacketBatch *batch) {
  int fd;
  uint32_t gen;
  std::tie(fd, gen) = opener_.GetCurrentFd();
  if (!opener_.IsValidFd(fd)) {
    return;
  }

  struct timeval tv;
  gettimeofday(&tv, nullptr);

  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *pkt = batch->pkts()[i];
    struct pcap_rec_hdr rec = {
        .ts_sec = (uint32_t)tv.tv_sec,
        .ts_usec = (uint32_t)tv.tv_usec,
        .incl_len = (uint32_t)pkt->head_len(),
        .orig_len = (uint32_t)pkt->total_len(),
    };

    struct iovec vec[2] = {{&rec, sizeof(rec)},
                           {pkt->head_data(), (size_t)pkt->head_len()}};

    int ret = writev(fd, vec, 2);
    if (ret < 0) {
      if (errno == EPIPE) {
        DLOG(WARNING) << "Broken pipe: stopping tcpdump";
        opener_.MarkDead(fd, gen);
      }
      return;
    }
  }
}

ADD_GATE_HOOK(Tcpdump, "tcpdump", "dump traffic on a network")
