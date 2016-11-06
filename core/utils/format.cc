#include "format.h"

#include <cassert>
#include <memory>

namespace bess {
namespace utils {

const std::string Format(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const std::string s = FormatVarg(fmt, ap);
  va_end(ap);
  return s;
}

const std::string FormatVarg(const char *fmt, va_list ap) {
  const int init_bufsize = 128;
  std::unique_ptr<char[]> buf;

  buf.reset(new char[init_bufsize]);
  int ret = vsnprintf(&buf[0], init_bufsize, fmt, ap);

  if (ret >= init_bufsize) {
    buf.reset(new char[ret + 1]);
    int new_ret;
    new_ret = vsnprintf(&buf[0], ret + 1, fmt, ap);
    assert(new_ret == ret);
  }
  return std::string(buf.get());
}

}  // namespace utils
}  // namespace bess
