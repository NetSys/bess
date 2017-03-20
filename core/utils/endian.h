#ifndef BESS_UTILS_ENDIAN_H_
#define BESS_UTILS_ENDIAN_H_

#include <cstdint>

namespace bess {
namespace utils {

constexpr bool is_be_system() {
  return (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__);
}

template <typename T>
class EndianBase {
 public:
  static constexpr T swap(const T &v);
};

template <>
class EndianBase<uint16_t> {
 public:
  static constexpr uint16_t swap(const uint16_t &v) {
    return __builtin_bswap16(v);
  }
};

template <>
class EndianBase<uint32_t> {
 public:
  static constexpr uint32_t swap(const uint32_t &v) {
    return __builtin_bswap32(v);
  }
};

template <>
class EndianBase<uint64_t> {
 public:
  static constexpr uint64_t swap(const uint64_t &v) {
    return __builtin_bswap64(v);
  }
};

template <typename T>
class[[gnu::packed]] BigEndian final : public EndianBase<T> {
 public:
  BigEndian() = default;
  BigEndian(const T &value) : value_(value) {}

  constexpr T to_cpu() const {
    return is_be_system() ? value_ : EndianBase<T>::swap(value_);
  }

  static constexpr T to_cpu(const T &v) {
    return is_be_system() ? v : EndianBase<T>::swap(v);
  }

  bool operator==(const BigEndian &other) const { return value_ == other.value_; }

  bool operator!=(const BigEndian &other) const { return value_ != other.value_; }

 protected:
  T value_;
};

using be16_t = BigEndian<uint16_t>;
using be32_t = BigEndian<uint32_t>;
using be64_t = BigEndian<uint64_t>;

// POD means trivial (no special constructor/destructor) and
// standard layout (i.e., binary compatible with C struct)
static_assert(std::is_pod<be16_t>::value, "not a POD type");
static_assert(std::is_pod<be32_t>::value, "not a POD type");
static_assert(std::is_pod<be64_t>::value, "not a POD type");

static_assert(sizeof(be16_t) == 2, "be16_t is not 2 bytes");
static_assert(sizeof(be32_t) == 4, "be32_t is not 4 bytes");
static_assert(sizeof(be64_t) == 8, "be64_t is not 8 bytes");

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ENDIAN_H_
