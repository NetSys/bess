#ifndef BESS_UTILS_VXLAN_H_
#define BESS_UTILS_VXLAN_H_

namespace bess {
namespace utils {

// 8-byte basic VXLAN header
// +-------+-------+-------+--------+
// | flags |       Reserved         |
// +-------+-------+-------+--------+
// |        VNI            | Rsvd.  |
// +-------+-------+-------+--------+
struct[[gnu::packed]] VxlanHeader {
  be32_t vx_flags;
  be32_t vx_vni;
};

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_VXLAN_H_
