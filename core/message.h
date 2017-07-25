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

#ifndef BESS_MESSAGE_H_
#define BESS_MESSAGE_H_

#include <cstdarg>
#include <functional>

#include "pb/bess_msg.pb.h"
#include "pb/error.pb.h"
#include "pb/module_msg.pb.h"

typedef bess::pb::Error pb_error_t;

using CommandResponse = bess::pb::CommandResponse;

CommandResponse CommandSuccess();
CommandResponse CommandSuccess(const google::protobuf::Message &return_data);

CommandResponse CommandFailure(int code);
[[gnu::format(printf, 2, 3)]] CommandResponse CommandFailure(int code,
                                                             const char *fmt,
                                                             ...);

template <typename T, typename M, typename A>
using pb_func_t = std::function<T(M *, const A &)>;

[[gnu::format(printf, 2, 3)]] pb_error_t pb_error(int code, const char *fmt,
                                                  ...);

static inline pb_error_t pb_errno(int code) {
  return pb_error(code, "%s", strerror(code));
}

#endif  // BESS_MESSAGE_H_
