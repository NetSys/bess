// C's sprintf-like functionality for std::string

#ifndef BESS_UTILS_FORMAT_H_
#define BESS_UTILS_FORMAT_H_

#include <cstdarg>
#include <string>

namespace bess {
namespace utils {

[[gnu::format(printf, 1, 2)]] std::string Format(const char *fmt, ...);
std::string FormatVarg(const char *fmt, va_list ap);

}  // namespace utils
}  // namespace bess

#endif  // BESS_UTILS_FORMAT_H_
