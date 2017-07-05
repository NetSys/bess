// Copyright (c) 2014-2016, The Regents of the University of California.
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

#ifndef BESS_MODULES_RANDOMUPDATE_H_
#define BESS_MODULES_RANDOMUPDATE_H_

#include "../module.h"
#include "../module_msg.pb.h"

#include "../utils/endian.h"
#include "../utils/random.h"

#define MAX_VARS 16

class RandomUpdate final : public Module {
 public:
  static const Commands cmds;

  RandomUpdate() : Module(), num_vars_(), vars_(), rng_() {}

  CommandResponse Init(const bess::pb::RandomUpdateArg &arg);

  void ProcessBatch(bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bess::pb::RandomUpdateArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  int num_vars_;

  struct {
    bess::utils::be32_t mask;  // bits with 1 won't be updated
    uint32_t min;
    uint32_t range;  // max - min + 1
    size_t offset;
    size_t bit_shift;
  } vars_[MAX_VARS];

  Random rng_;
};

#endif  // BESS_MODULES_RANDOMUPDATE_H_
