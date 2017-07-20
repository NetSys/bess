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

#include "pcap_handle.h"

#include "pcap.h"

PcapHandle::PcapHandle(const std::string& dev) : handle_() {
  char errbuf[PCAP_ERRBUF_SIZE];
  handle_ = pcap_open_live(dev.c_str(), PCAP_SNAPLEN, 1, -1, errbuf);
}

PcapHandle::PcapHandle(pcap_t *handle) : handle_(handle) {}

PcapHandle::PcapHandle(PcapHandle&& other) : handle_(other.handle_) {
  other.handle_ = nullptr;
}

PcapHandle& PcapHandle::operator=(PcapHandle&& other) {
  if (this != &other) {
    if (is_initialized()) {
      Reset();
    }
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }
  return *this;
}

PcapHandle::~PcapHandle() {
  Reset();
}

void PcapHandle::Reset() {
  if (is_initialized()) {
    pcap_close(handle_);
    handle_ = nullptr;
  }
}

int PcapHandle::SendPacket(const u_char* packet, int len) {
  if (is_initialized()) {
    return pcap_sendpacket(handle_, packet, len);
  } else {
    return -1;
  }
}

const u_char* PcapHandle::RecvPacket(int* caplen) {
  if (!is_initialized()) {
    *caplen = 0;
    return nullptr;
  }
  pcap_pkthdr header;
  const u_char* pkt = pcap_next(handle_, &header);
  *caplen = header.caplen;
  return pkt;
}

int PcapHandle::SetBlocking(bool block) {
  char errbuf[PCAP_ERRBUF_SIZE];
  return pcap_setnonblock(handle_, block ? 0 : 1, errbuf);
}
