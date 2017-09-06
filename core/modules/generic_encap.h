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

#ifndef BESS_MODULES_GENERICENCAP_H_
#define BESS_MODULES_GENERICENCAP_H_

#include "../module.h"
#include "../pb/module_msg.pb.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8

struct Field {
  uint64_t value; /* onlt for constant values */
  int attr_id;    /* -1 for constant values */
  int pos;        /* relative position in the new header */
  int size;       /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

class GenericEncap final : public Module {
 public:
  GenericEncap() : Module(), encap_size_(), num_fields_(), fields_() {
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const bess::pb::GenericEncapArg &arg);

  void ProcessBatch(bess::PacketBatch *batch);

 private:
  CommandResponse AddFieldOne(const bess::pb::GenericEncapArg_EncapField &field,
                              struct Field *f, int idx);

  int encap_size_;

  int num_fields_;

  struct Field fields_[MAX_FIELDS];
};

#endif  // BESS_MODULES_GENERICENCAP_H_
