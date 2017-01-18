#ifndef BESS_UTILS_TCP_FLOW_RECONSTRUCT_H_
#define BESS_UTILS_TCP_FLOW_RECONSTRUCT_H_

#include <arpa/inet.h>

#include <vector>

#include "ether.h"
#include "ip.h"
#include "../mem_alloc.h"
#include "../packet.h"
#include "tcp.h"

namespace bess {
namespace utils {

// A utility class that accumulates TCP packet data in the correct order.
class TcpFlowReconstruct {
 public:
  // Constructs a TCP flow reconstruction object that can hold initial_buflen
  // bytes to start with.  If initial_buflen is zero, it is automatically set to
  // 1.
  TcpFlowReconstruct(uint32_t initial_buflen)
      : initialized_(false),
        init_seq_(0),
        buflen_(initial_buflen),
        received_map_(initial_buflen),
        contiguous_len_(0) {
    if (buflen_ <= 0) {
      buflen_ = 1;
      received_map_.resize(buflen_);
    }

    buf_ = (char *) mem_alloc(buflen_);
  }

  ~TcpFlowReconstruct() {
    free(buf_);
  }

  // Returns the underlying buffer of reconstructed flow bytes.  Not guaranteed
  // to return the same pointer between calls to InsertPacket().
  const char *buf() const { return buf_; }

  // Returns the underlying bitmap of which bytes in buf have been received.
  // Contents are updated by each call to InsertPacket().
  const std::vector<bool> &received_map() const { return received_map_; }

  // Returns the initial data sequence number extracted from the SYN.
  uint32_t init_seq() const { return init_seq_; }

  // Returns the length of contiguous data available in the buffer starting from
  // the beginning.  Updated every time InsertPacket() is called.
  size_t contiguous_len() const { return contiguous_len_; }

  // Adds the data of the given packet based upon its TCP sequence number.  If
  // the packet is a SYN then we use the SYN to set the initial sequence number
  // offset.
  //
  // Returns true upon success.  Returns false if the given packet is not a SYN
  // but if we have not been given a SYN previously.
  //
  // Behavior is undefined the packet is not a TCP packet.
  bool InsertPacket(Packet *p) {
    struct EthHeader *eth = p->head_data<struct EthHeader *>();
    struct Ipv4Header *ip = (struct Ipv4Header *) (eth + 1);
    struct TcpHeader *tcp = (struct TcpHeader *) (((char *) ip) + (ip->header_length * 4));

    // Assumes we only get one SYN and the sequence number of it doesn't change
    // for any reason.  Also assumes we have no data in the SYN.
    if (tcp->flags & TCP_FLAG_SYN) {
     init_seq_ = ntohl(tcp->seq_num) + 1;
     initialized_ = true;
     return true;
    }

    if (!initialized_) {
      return false;
    }

    uint32_t buf_offset = ntohl(tcp->seq_num) - init_seq_;
    char *datastart = ((char *) tcp) + (tcp->offset * 4);
    uint32_t datalen = ntohs(ip->length) - (tcp->offset * 4) -
                       (ip->header_length * 4);

    // If we will run out of space, make more room.
    while ((buf_offset + datalen) > buflen_) {
      buflen_ *= 2;
      buf_ = (char *) mem_realloc(buf_, buflen_);
      received_map_.resize(buflen_);
    }

    memcpy(buf_ + buf_offset, datastart, datalen);

    // Mark that we've received the specified bits.
    for (size_t i = buf_offset; i < datalen + buf_offset; ++i) {
      received_map_[i] = true;
    }

    while (received_map_[contiguous_len_]) {
      ++contiguous_len_;
    }

    return true;
  }

 private:
  // Tracks whether the init_seq_ (and thus this object) has been initialized with a SYN.
  bool initialized_;  

  // The initial sequence number of data bytes in the TCP flow.
  uint32_t init_seq_;  

  // A buffer (potentially with holes) of received data.
  char *buf_;

  // The length of the buffer.
  uint32_t buflen_;

  // A bitmap of which bytes have already been received in buf_.
  std::vector<bool> received_map_;

  // The length of contiguous received data starting from buf_[0].
  size_t contiguous_len_;

  DISALLOW_COPY_AND_ASSIGN(TcpFlowReconstruct);
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_TCP_FLOW_RECONSTRUCT_H_
