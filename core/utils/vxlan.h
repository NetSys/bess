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
struct[[gnu::packed]] Vxlan {
  be32_t vx_flags;
  be32_t vx_vni;
};

static_assert(std::is_pod<Vxlan>::value, "not a POD type");
static_assert(sizeof(Vxlan) == 8, "struct Vxlan is incorrect");

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_VXLAN_H_
