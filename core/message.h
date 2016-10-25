#ifndef _MESSAGE_H_
#define _MESSAGE_H_

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
  pb_error_t p = pb_error_details(code, NULL, fmt, ap);
  va_end(ap);
  return p;
}

static inline pb_error_t pb_errno_details(int code, const char *details) {
  return pb_error_details(code, details, "%s", strerror(code));
}

static inline pb_error_t pb_errno(int code) {
  return pb_errno_details(code, NULL);
}

#endif
