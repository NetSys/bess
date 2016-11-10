#include "format.h"

#include <cassert>
#include <memory>

namespace bess {
namespace utils {

std::string FormatVarg(const char *fmt, va_list ap) {
  char *ptr = nullptr;
  int len = vasprintf(&ptr, fmt, ap);
  if (len < 0)
    return "<FormatVarg() error>";

  std::string ret(ptr, len);
  free(ptr);
  return ret;
}

std::string Format(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const std::string s = FormatVarg(fmt, ap);
  va_end(ap);
  return s;
}

int ParseVarg(const std::string &s, const char *fmt, va_list ap) {
  return vsscanf(s.c_str(), fmt, ap);
}

int Parse(const std::string &s, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int ret = ParseVarg(s, fmt, ap);
  va_end(ap);
  return ret;
}

}  // namespace utils
}  // namespace bess
