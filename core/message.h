#ifndef BESS_CORE_MESSAGE_H_
#define BESS_CORE_MESSAGE_H_

#include "bess_message.pb.h"
#include "error.pb.h"
#include "module_message.pb.h"
#include <stdarg.h>

typedef bess::Error pb_error_t;

const std::string string_vformat(const char *fmt, va_list ap);

const std::string string_format(const char *fmt, ...);

pb_error_t pb_error_details(int code, const char *details, const char *fmt,
                            ...);

static inline pb_error_t pb_error(int code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  pb_error_t p = pb_error_details(code, nullptr, fmt, ap);
  va_end(ap);
  return p;
}

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

  if (val)
    return -EINVAL; /* the value is too large for the size */
  else
    return 0;
}

#endif
