#ifndef BESS_UTILS_UDP_H_
#define BESS_UTILS_UDP_H_

namespace bess {
namespace utils {

// A basic UDP header definition.
struct[[gnu::packed]] UdpHeader {
  uint16_t src_port;  // Source port.
  uint16_t dst_port;  // Destination port.
  uint16_t length;    // Length of header and data.
  uint16_t checksum;  // Checksum.
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_UDP_H_
