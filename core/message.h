#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "bessctl.pb.h"
#include <stdarg.h>

typedef std::unique_ptr<bess::Error> error_ptr_t;

const std::string string_format(const char *fmt, ...);

error_ptr_t pb_error_details(int code, const char *details, const char *fmt,
                             ...);

static inline error_ptr_t pb_error(int code, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  error_ptr_t p = pb_error_details(code, NULL, fmt, ap);
  va_end(ap);
  return p;
}

static inline error_ptr_t pb_errno_details(int code, const char *details) {
  return pb_error_details(code, details, "%s", strerror(code));
}

static inline error_ptr_t pb_errno(int code) {
  return pb_errno_details(code, NULL);
}

#endif
