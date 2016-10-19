#include "message.h"

#include <memory>

const std::string string_format(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const std::string s = string_format(fmt, ap);
  va_end(ap);
  return s;
}

static const std::string string_vformat(const char *fmt, va_list ap) {
  const int init_bufsize = 128;
  std::unique_ptr<char[]> buf;

  int ret;

  buf.reset(new char[init_bufsize]);
  ret = vsnprintf(&buf[0], init_bufsize, fmt, ap);

  if (ret >= init_bufsize) {
    buf.reset(new char[ret + 1]);
    int new_ret;
    new_ret = vsnprintf(&buf[0], ret + 1, fmt, ap);
    assert(new_ret == ret);
  }
  return std::string(buf.get());
}

error_ptr_t pb_error_details(int code, const char *details, const char *fmt,
                             ...) {
  error_ptr_t p(new bess::Error());

  p->set_err(code);

  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    const std::string message = string_vformat(fmt, ap);
    va_end(ap);
    p->set_errmsg(message);
  }

  if (details) {
    p->set_details(details);
  }

  return p;
}
