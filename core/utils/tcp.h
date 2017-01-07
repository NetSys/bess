#ifndef BESS_UTILS_TCP_H_

// Flags used in the flags field of the TCP header.
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PUSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

namespace bess {
namespace utils {

// A basic TCP header definition loosely based on the BSD version.
struct [[gnu::packed]] TcpHeader {
  uint16_t src_port;    // Source port.
  uint16_t dst_port;    // Destination port.
  uint32_t seq_num;     // Sequence number.
  uint32_t ack_num;     // Acknowledgement number.
#if __BYTE_ORDER == __LITTLE_ENDIAN
  uint8_t reserved:4;   // Unused reserved bits.
  uint8_t offset:4;     // Data offset.
#elif __BYTE_ORDER == __BIG_ENDIAN
  uint8_t offset:4;     // Data offset.
  uint8_t reserved:4;   // Unused reserved bits.
#else 
#error __BYTE_ORDER must be defined.
#endif
  uint8_t flags;        // Flags.
  uint16_t window;      // Receive window.
  uint16_t checksum;    // Checksum.
  uint16_t urgent_ptr;  // Urgent pointer.
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_TCP_H_
