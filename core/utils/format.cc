// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

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
