#include "message.h"

#include <string>

#include "utils/format.h"

pb_error_t pb_error(int code, const char *fmt, ...) {
  pb_error_t p;

  p.set_err(code);

  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    const std::string message = bess::utils::FormatVarg(fmt, ap);
    va_end(ap);
    p.set_errmsg(message);
  }

  return p;
}
