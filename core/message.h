#ifndef BESS_MESSAGE_H_
#define BESS_MESSAGE_H_

#include <cstdarg>

#include "bess_msg.pb.h"
#include "error.pb.h"

typedef bess::pb::Error pb_error_t;
typedef bess::pb::ModuleCommandResponse pb_cmd_response_t;

template <typename T, typename M, typename A>
using pb_func_t = std::function<T(M *, const A &)>;

[[gnu::format(printf, 2, 3)]] pb_error_t pb_error(int code, const char *fmt,
                                                  ...);

static inline pb_error_t pb_errno(int code) {
  return pb_error(code, "%s", strerror(code));
}

static inline int uint64_to_bin(uint8_t *ptr, int size, uint64_t val, int be) {
  memset(ptr, 0, size);

  if (be) {
    for (int i = size - 1; i >= 0; i--) {
      ptr[i] = val & 0xff;
      val >>= 8;
    }
  } else {
    for (int i = 0; i < size; i++) {
      ptr[i] = val & 0xff;
      val >>= 8;
    }
  }

  if (val) {
    return -EINVAL; /* the value is too large for the size */
  } else {
    return 0;
  }
}

// move data from *ptr to *val. set "be" if *ptr stores big endian data
static inline int bin_to_uint64(uint64_t *val, uint8_t *ptr, int size, bool be) {
  if(size > 8 || size < 1){
    return -EINVAL; /* Size must be 1-8 */
  }

  *val = 0;

  if (be) {
    for (int i = 0; i < size; i++) {
      *val = (*val << 8) | ptr[i];
    }
  } else {
    for (int i = size - 1; i >= 0; i--) {
      *val = (*val << 8) | ptr[i];
    }
  }
  return 0;
}
#endif  // BESS_MESSAGE_H_
