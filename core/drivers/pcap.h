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

#ifndef BESS_DRIVERS_PCAP_H_
#define BESS_DRIVERS_PCAP_H_

#include "../port.h"

#include <glog/logging.h>

#include "../utils/pcap_handle.h"

// Port to connect to a device via PCAP.
// (Not recommended because PCAP is slow :-)
// This driver is experimental. Currently does not support mbuf chaining and
// needs more tests!
class PCAPPort final : public Port {
 public:
  CommandResponse Init(const bess::pb::PCAPPortArg &arg);

  void DeInit() override;
  // PCAP has no notion of queue so unlike parent (port.cc) quid is ignored.
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;
  // Ditto above: quid is ignored.
  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

 private:
  void GatherData(unsigned char *data, bess::Packet *pkt);
  PcapHandle pcap_handle_;
};

#endif  // BESS_DRIVERS_PCAP_H_
