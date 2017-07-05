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

#include "message.h"

#include <string>

#include "utils/format.h"

pb_error_t pb_error(int code, const char *fmt, ...) {
  pb_error_t p;

  p.set_code(code);

  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    const std::string message = bess::utils::FormatVarg(fmt, ap);
    va_end(ap);
    p.set_errmsg(message);
  }

  return p;
}

using CommandError = bess::pb::Error;

CommandResponse CommandSuccess() {
  return CommandResponse();
}

CommandResponse CommandSuccess(const google::protobuf::Message &return_data) {
  CommandResponse ret;
  ret.mutable_data()->PackFrom(return_data);
  return ret;
}

CommandResponse CommandFailure(int code) {
  CommandResponse ret;
  CommandError *error = ret.mutable_error();
  error->set_code(code);
  error->set_errmsg(strerror(code));
  return ret;
}

CommandResponse CommandFailure(int code, const char *fmt, ...) {
  CommandResponse ret;
  CommandError *error = ret.mutable_error();

  error->set_code(code);

  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    const std::string message = bess::utils::FormatVarg(fmt, ap);
    va_end(ap);
    error->set_errmsg(message);
  }

  return ret;
}
