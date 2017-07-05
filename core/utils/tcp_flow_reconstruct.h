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

#ifndef BESS_UTILS_TCP_FLOW_RECONSTRUCT_H_
#define BESS_UTILS_TCP_FLOW_RECONSTRUCT_H_

#include <vector>

#include "../packet.h"
#include "copy.h"
#include "ether.h"
#include "ip.h"
#include "tcp.h"

namespace bess {
namespace utils {

// A utility class that accumulates TCP packet data in the correct order.
class TcpFlowReconstruct {
 public:
  // Constructs a TCP flow reconstruction object that can hold initial_buflen
  // bytes to start with.
  explicit TcpFlowReconstruct(size_t initial_buflen = 1024)
      : initialized_(false), init_seq_(0), buf_(initial_buflen) {}

  virtual ~TcpFlowReconstruct() {}

  // Returns the underlying buffer of reconstructed flow bytes.  Not guaranteed
  // to return the same pointer between calls to InsertPacket().
  const char *buf() const { return buf_.data(); }

  // Returns the size of the underlying buffer
  size_t buf_size() const { return buf_.size(); }

  // Returns the initial data sequence number extracted from the SYN.
  uint32_t init_seq() const { return init_seq_; }

  // Returns the length of contiguous data available in the buffer starting from
  // the beginning.  Updated every time InsertPacket() is called.
  size_t contiguous_len() const {
    const auto it = received_map_.begin();
    return (it == received_map_.end()) ? 0 : (it->second - it->first);
  }

  // Adds the data of the given packet based upon its TCP sequence number.  If
  // the packet is a SYN then we use the SYN to set the initial sequence number
  // offset.
  //
  // Returns true upon success.  Returns false if the given packet is not a SYN
  // but if we have not been given a SYN previously.
  //
  // Behavior is undefined the packet is not a TCP packet.
  bool InsertPacket(Packet *p) {
    const Ethernet *eth = p->head_data<const Ethernet *>();
    const Ipv4 *ip = (const Ipv4 *)(eth + 1);
    const Tcp *tcp =
        (const Tcp *)(((const char *)ip) + (ip->header_length * 4));

    uint32_t seq = tcp->seq_num.value();
    // Assumes we only get one SYN and the sequence number of it doesn't change
    // for any reason.  Also assumes we have no data in the SYN.
    if (tcp->flags & Tcp::Flag::kSyn) {
      init_seq_ = seq + 1;
      initialized_ = true;
      return true;
    }

    if (!initialized_) {
      VLOG(1) << "Non-SYN received but not yet initialized.";
      return false;
    }

    // Check if the sequence number is greater or equal than SYN + 1. Wraparound
    // is possible.
    if ((int32_t)(seq - init_seq_) < 0) {
      VLOG(1) << "Sequence number not match. Initial seq: " << init_seq_
              << ", Seq: " << seq;
      return false;
    }

    // Wraparound is possible.
    uint32_t buf_offset = seq - init_seq_;

    const char *datastart = ((const char *)tcp) + (tcp->offset * 4);
    uint32_t datalen =
        ip->length.value() - (tcp->offset * 4) - (ip->header_length * 4);

    // pure-ACK packets?
    if (datalen == 0) {
      return true;
    }

    // If we will run out of space, make more room.
    if ((buf_offset + datalen) > buf_.size()) {
      size_t new_buflen = (buf_offset + datalen) * 2;
      buf_.resize(new_buflen);
    }

    bess::utils::CopyInlined(buf_.data() + buf_offset, datastart, datalen);

    uint32_t start = buf_offset;
    uint32_t end = buf_offset + datalen;

    // Merge the new new data with existing segments
    // new segment                           |-------------|
    // existing segments with a hole   |---A---|   |--B--|-C-|
    //                                             ^
    //                                             lower_bound(start)
    auto it = received_map_.lower_bound(start);
    if (it != received_map_.begin()) {
      auto it_prev = it;
      it_prev--;
      if (it_prev->second >= start) {
        // The segment right before the lower_bound(start) may partially overlap
        // with the new segment (e.g., segment A in the above figure).
        // Include the segment for merging
        start = it_prev->first;
        it = it_prev;
      }
    }

    // Remove all ovlerapping segments
    while (it != received_map_.end() && it->first <= end) {
      end = std::max(end, it->second);
      it = received_map_.erase(it);
    }

    // Insert the merged segment
    received_map_.emplace(start, end);

    return true;
  }

 private:
  // Tracks whether the init_seq_ (and thus this object) has been initialized
  // with a SYN.
  bool initialized_;

  // The initial sequence number of data bytes in the TCP flow.
  uint32_t init_seq_;

  // A buffer (potentially with holes) of received data.
  std::vector<char> buf_;

  // Sorted list of received segments. Segments are merged as necessary.
  // Key: offset from init_seq_
  // T: end offset of the segment
  std::map<uint32_t, uint32_t> received_map_;

  DISALLOW_COPY_AND_ASSIGN(TcpFlowReconstruct);
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_TCP_FLOW_RECONSTRUCT_H_
