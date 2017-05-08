#include "endian.h"

namespace bess {
namespace utils {

bool uint64_to_bin(void *ptr, uint64_t val, size_t size, bool big_endian) {
  uint8_t *const ptr8 = static_cast<uint8_t *>(ptr);

  if (big_endian) {
    for (uint8_t *p = ptr8 + size - 1; p >= ptr8; p--) {
      *p = val & 0xff;
      val >>= 8;
    }
  } else {
    for (uint8_t *p = ptr8; p < ptr8 + size; p++) {
      *p = val & 0xff;
      val >>= 8;
    }
  }

  if (val) {
    return false;  // the value is too large for the size
  } else {
    return true;
  }
}

bool bin_to_uint64(uint64_t *pval, const void *ptr, size_t size,
                   bool big_endian) {
  const uint8_t *const ptr8 = static_cast<const uint8_t *>(ptr);

  if (size > 8 || size < 1) {
    return false;  // size must be 1-8
  }

  *pval = 0;

  if (big_endian) {
    for (const uint8_t *p = ptr8; p < ptr8 + size; p++) {
      *pval = (*pval << 8) | *p;
    }
  } else {
    for (const uint8_t *p = ptr8 + size - 1; p >= ptr8; p--) {
      *pval = (*pval << 8) | *p;
    }
  }

  return true;
}

}  // namespace utils
}  // namespace bess
