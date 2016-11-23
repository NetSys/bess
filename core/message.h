#ifndef BESS_MESSAGE_H_
#define BESS_MESSAGE_H_

#include <cstdarg>

#include "bess_msg.pb.h"
#include "error.pb.h"

typedef bess::pb::Error pb_error_t;
typedef bess::pb::ModuleCommandResponse pb_cmd_response_t;

template <typename T, typename M, typename A>
using pb_func_t = std::function<T(M *, const A &)>;

[[gnu::format(printf, 3, 4)]] pb_error_t pb_error_details(int code,
                                                          const char *details,
                                                          const char *fmt, ...);

#define pb_error(code, fmt, ...) \
  pb_error_details(code, nullptr, fmt, ##__VA_ARGS__)

static inline pb_error_t pb_errno_details(int code, const char *details) {
  return pb_error_details(code, details, "%s", strerror(code));
}

static inline pb_error_t pb_errno(int code) {
  return pb_errno_details(code, nullptr);
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

#endif  // BESS_MESSAGE_H_
