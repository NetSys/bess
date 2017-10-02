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

#include "pcapng.h"

#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <limits>

#include <glog/logging.h>

#include "../message.h"
#include "../utils/common.h"
#include "../utils/pcapng.h"
#include "../utils/time.h"

using namespace bess::utils::pcapng;

namespace {

// Return `a` rounded up to the nearest multiple of `b`
template <typename T>
T RoundUp(T a, T b) {
  return ((a + (b - 1)) / b) * b;
}

// Return the number of bytes of padding to align a buffer long `a` to
// `b` units.
template <typename T>
T PadSize(T a, T b) {
  return RoundUp(a, b) - a;
}

// Return the single hex digit representing `nibble`.  If it cannot be
// represented, return the char 'X'.
char NibbleToHD(char nibble) {
  if (nibble >= 0 && nibble <= 9) {
    return nibble + '0';
  } else if (nibble >= 10 && nibble <= 15) {
    return nibble - 10 + 'A';
  } else {
    return 'X';
  }
}

// Represent the buffer `src` (long `len` bytes) as an hex string.  Put
// the result into `dst` (which must be at least `len * 2` bytes long).
void BytesToHexDump(const void *src, size_t len, char *dst) {
  for (size_t i = 0; i < len; i++) {
    char byte = static_cast<const char *>(src)[i];
    dst[i * 2] = NibbleToHD((byte >> 4) & 0xF);
    dst[i * 2 + 1] = NibbleToHD(byte & 0xF);
  }
}

}  // namespace

const std::string Pcapng::kName = "pcapng";

Pcapng::Pcapng()
    : bess::GateHook(Pcapng::kName, Pcapng::kPriority),
      fifo_fd_(-1),
      attrs_(),
      attr_template_() {}

Pcapng::~Pcapng() {
  if (fifo_fd_ >= 0) {
    close(fifo_fd_);
  }
}

CommandResponse Pcapng::Init(const bess::Gate *gate,
                             const bess::pb::PcapngArg &arg) {
  Module *m = gate->module();
  std::string tmpl;
  int ret;

  size_t i = 0;
  for (const auto &it : m->all_attrs()) {
    tmpl += it.name + " = ";

    size_t tmpl_offset = tmpl.size();

    tmpl += std::string(it.size * 2, 'X') + " ";

    if (tmpl.size() > std::numeric_limits<uint16_t>::max()) {
      // Doesn't fit in the option string.
      break;
    }

    attrs_.emplace_back(Attr{.md_offset = m->attr_offset(i),
                             .size = it.size,
                             .tmpl_offset = tmpl_offset});
    i++;
  }

  if (!tmpl.empty() && tmpl.back() == ' ') {
    tmpl.pop_back();
  }

  if (tmpl.size() > std::numeric_limits<uint16_t>::max()) {
    tmpl.resize(std::numeric_limits<uint16_t>::max());
  }

  attr_template_ = std::vector<char>(tmpl.begin(), tmpl.end());

  fifo_fd_ = open(arg.fifo().c_str(), O_WRONLY | O_NONBLOCK);
  if (fifo_fd_ < 0) {
    return CommandFailure(-errno, "Failed to open FIFO");
  }

  ret = fcntl(fifo_fd_, F_SETFL, fcntl(fifo_fd_, F_GETFL) | O_NONBLOCK);
  if (ret < 0) {
    close(fifo_fd_);
    return CommandFailure(-errno, "fnctl() on FIFO failed");
  }

  SectionHeaderBlock shb = {
      .type = SectionHeaderBlock::kType,
      .tot_len = sizeof(shb) + sizeof(uint32_t),
      .bom = SectionHeaderBlock::kBom,
      .maj_ver = SectionHeaderBlock::kMajVer,
      .min_ver = SectionHeaderBlock::kMinVer,
      .sec_len = -1,
  };

  uint32_t shb_tot_len = shb.tot_len;

  InterfaceDescriptionBlock idb = {
      .type = InterfaceDescriptionBlock::kType,
      .tot_len = sizeof(idb) + sizeof(uint32_t),
      .link_type = InterfaceDescriptionBlock::kEthernet,
      .reserved = 0,
      .snap_len = 1518,
  };

  uint32_t idb_tot_len = idb.tot_len;

  struct iovec vec[4] = {{&shb, sizeof(shb)},
                         {&shb_tot_len, sizeof(shb_tot_len)},
                         {&idb, sizeof(idb)},
                         {&idb_tot_len, sizeof(idb_tot_len)}};

  ret = writev(fifo_fd_, vec, 4);
  if (ret < 0) {
    close(fifo_fd_);
    return CommandFailure(-errno, "Failed to write PCAP header");
  }

  return CommandSuccess();
}

void Pcapng::ProcessBatch(const bess::PacketBatch *batch) {
  struct timeval tv;

  int ret = 0;

  gettimeofday(&tv, nullptr);

  uint64_t ts = tv.tv_sec * 1000000 + tv.tv_usec;
  uint16_t comment_size = static_cast<uint16_t>(attr_template_.size());

  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *pkt = batch->pkts()[i];

    Option opt_comment = {
        .code = Option::kComment, .len = comment_size,
    };

    for (const Attr &attr : attrs_) {
      const char *attr_data = ptr_attr_with_offset<char>(attr.md_offset, pkt);
      if (attr_data != nullptr) {
        BytesToHexDump(attr_data, attr.size, &attr_template_[attr.tmpl_offset]);
      } else {
        auto string_it = attr_template_.begin() + attr.tmpl_offset;
        std::fill(string_it, string_it + attr.size * 2, 'X');
      }
    }

    Option opt_end = {
        .code = Option::kEndOfOpts, .len = 0,
    };

    EnhancedPacketBlock epb = {
        .type = EnhancedPacketBlock::kType,
        .tot_len = static_cast<uint32_t>(
            sizeof(epb) + sizeof(uint32_t) +
            RoundUp<uint32_t>(pkt->head_len(), 4) + sizeof(opt_comment) +
            RoundUp<uint32_t>(comment_size, 4) + sizeof(opt_end)),
        .interface_id = 0,
        .timestamp_high = static_cast<uint32_t>(ts >> 32),
        .timestamp_low = static_cast<uint32_t>(ts),
        .captured_len = static_cast<uint32_t>(pkt->head_len()),
        .orig_len = static_cast<uint32_t>(pkt->total_len()),
    };

    uint32_t padding = 0;

    uint32_t epb_tot_len = epb.tot_len;

    struct iovec vec[8] = {
        {&epb, sizeof(epb)},
        {pkt->head_data(), static_cast<size_t>(pkt->head_len())},
        {&padding, static_cast<size_t>(PadSize(pkt->head_len(), 4))},

        {&opt_comment, sizeof(opt_comment)},
        {attr_template_.data(), comment_size},
        {&padding, static_cast<size_t>(PadSize<uint32_t>(comment_size, 4))},
        {&opt_end, sizeof(opt_end)},

        {&epb_tot_len, sizeof(epb_tot_len)}};

    ret = writev(fifo_fd_, vec, 8);
    if (ret < 0) {
      if (errno == EPIPE) {
        DLOG(WARNING) << "Broken pipe: stopping pcapng";
        close(fifo_fd_);
        fifo_fd_ = -1;
      }
      return;
    }
  }
}

ADD_GATE_HOOK(Pcapng)
