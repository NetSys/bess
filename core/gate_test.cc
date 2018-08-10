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

#include "gate.h"

#include <gtest/gtest.h>

#include "gate_hooks/tcpdump.h"
#include "gate_hooks/track.h"
#include "module.h"
#include "pktbatch.h"

namespace bess {

class GateTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    g = new Gate(nullptr, 0);
    ASSERT_NE(nullptr, g);
  }

  virtual void TearDown() { delete g; }

  int AddHook(GateHook *hook) { return g->AddHook(hook); }

  Gate *g;
};

class IOGateTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    og = new OGate(nullptr, 0, nullptr);
    ig = new IGate(nullptr, 0);
    ASSERT_NE(nullptr, og);
    ASSERT_NE(nullptr, ig);
  }

  virtual void TearDown() {
    delete og;
    delete ig;
  }

  OGate *og;
  IGate *ig;
};

TEST_F(GateTest, AllowMultipleHook) {
  GateHook *track0 = new Track();
  track0->set_name("track0");
  ASSERT_EQ(0, AddHook(track0));
  GateHook *track1 = new Track();
  track1->set_name("track1");
  ASSERT_EQ(0, AddHook(track1));
}

TEST_F(GateTest, MultipleHookSameNameFail) {
  GateHook *track0 = new Track();
  track0->set_name("track0");
  ASSERT_EQ(0, AddHook(track0));
  GateHook *track1 = new Track();
  track1->set_name("track0");
  ASSERT_EQ(EEXIST, AddHook(track1));
  delete track1;
}

TEST_F(GateTest, HookPriority) {
  ASSERT_EQ(0, AddHook(new Track()));
  ASSERT_EQ(0, AddHook(new Tcpdump()));
  ASSERT_EQ(Track::kName, g->hooks()[0]->class_name());
}

TEST_F(GateTest, FindHook) {
  ASSERT_EQ(nullptr, g->FindHookByClass(Track::kName));
  ASSERT_EQ(0, AddHook(new Track()));
  ASSERT_NE(nullptr, g->FindHookByClass(Track::kName));
}

TEST_F(GateTest, RemoveHook) {
  ASSERT_EQ(0, AddHook(new Track()));
  g->RemoveHook(Track::kName);
  ASSERT_EQ(nullptr, g->FindHook(Track::kName));
}

TEST_F(IOGateTest, OGate) {
  og->SetIgate(ig);
  ASSERT_EQ(ig, og->igate());
  ASSERT_EQ(0, og->igate_idx());
}

TEST_F(IOGateTest, IGate) {
  ig->PushOgate(og);
  ASSERT_EQ(og, ig->ogates_upstream()[0]);
  ig->RemoveOgate(og);
  ASSERT_EQ(0, ig->ogates_upstream().size());
}
}  // namespace bess
