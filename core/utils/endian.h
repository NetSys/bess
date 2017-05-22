#ifndef BESS_UTILS_ENDIAN_H_
#define BESS_UTILS_ENDIAN_H_

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

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

// NOTE: DO NOT ADD implicit type-conversion, comparison, and operations.
//       Especially, we do not allow assignments like "be16_t a = 5;".
//       While it looks benign, this form of assignment becomes quite
//       confusing if the rhs is a variable; the behavior of the assignment
//       will be different depending on whether rhs is native or big endian,
//       which may not be immediately clear from the variable name.
template <typename T>
class[[gnu::packed]] BigEndian final : public EndianBase<T> {
 public:
  BigEndian() = default;
  BigEndian(const BigEndian<T> &o) = default;

  explicit constexpr BigEndian(const T &cpu_value)
      : data_(is_be_system() ? cpu_value : EndianBase<T>::swap(cpu_value)) {}

  constexpr T value() const {
    return is_be_system() ? data_ : EndianBase<T>::swap(data_);
  }

  constexpr T raw_value() const { return data_; }

  constexpr BigEndian<T> operator~() const {
    return BigEndian(~(this->value()));
  }

  // While this swap(swap(a) & swap(b)) looks inefficient, gcc 4.9+ will
  // correctly optimize the code.
  constexpr BigEndian<T> operator&(const BigEndian<T> &o) const {
    return BigEndian(this->value() & o.value());
  }

  constexpr BigEndian<T> operator|(const BigEndian<T> &o) const {
    return BigEndian(this->value() | o.value());
  }

  constexpr BigEndian<T> operator^(const BigEndian<T> &o) const {
    return BigEndian(this->value() ^ o.value());
  }

  constexpr BigEndian<T> operator+(const BigEndian<T> &o) const {
    return BigEndian(this->value() + o.value());
  }

  constexpr BigEndian<T> operator-(const BigEndian<T> &o) const {
    return BigEndian(this->value() - o.value());
  }

  constexpr BigEndian<T> operator<<(size_t shift) const {
    return BigEndian(this->value() << shift);
  }

  constexpr BigEndian<T> operator>>(size_t shift) const {
    return BigEndian(this->value() >> shift);
  }

  constexpr bool operator==(const BigEndian &o) const {
    return data_ == o.data_;
  }

  constexpr bool operator!=(const BigEndian &o) const { return !(*this == o); }

  constexpr bool operator<(const BigEndian &o) const {
    return this->value() < o.value();
  }

  constexpr bool operator>(const BigEndian &o) const { return o < *this; }

  constexpr bool operator<=(const BigEndian &o) const { return !(*this > o); }

  constexpr bool operator>=(const BigEndian &o) const { return !(*this < o); }

  explicit constexpr operator bool() const { return data_ != 0; }

  friend std::ostream &operator<<(std::ostream &os, const BigEndian &be) {
    os << "0x" << std::hex << std::setw(sizeof(be) * 2) << std::setfill('0')
       << be.value() << std::setfill(' ') << std::dec;
    return os;
  }

 protected:
  T data_;  // stored in big endian in memory
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

// Copy data from val to *ptr. Set "big_endian" to store in big endian
bool uint64_to_bin(void *ptr, uint64_t val, size_t size, bool big_endian);

// Copy data from *ptr to *pval. Set "big_endian" if *ptr has big endian data
bool bin_to_uint64(uint64_t *pval, const void *ptr, size_t size,
                   bool big_endian);

// this is to make sure BigEndian has constexpr constructor and value()
static_assert(be32_t(0x1234).value() == 0x1234, "Something is wrong");

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_ENDIAN_H_
