#include "gate.h"

#include <gtest/gtest.h>

#include "hooks/tcpdump.h"
#include "hooks/track.h"
#include "module.h"
#include "pktbatch.h"

namespace bess {

class GateTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    g = new Gate(nullptr, 0, nullptr);
    ASSERT_EQ(nullptr, g->arg());
  }

  virtual void TearDown() { delete g; }

  Gate *g;
};

class IOGateTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    og = new OGate(nullptr, 0, nullptr);
    ig = new IGate(nullptr, 0, nullptr);
    ASSERT_NE(nullptr, og);
    ASSERT_NE(nullptr, ig);
  }

  virtual void TearDown() {
    og->ClearHooks();
    ig->ClearHooks();
    delete og;
    delete ig;
  }

  OGate *og;
  IGate *ig;
};

TEST_F(GateTest, AddExistingHookFails) {
  ASSERT_EQ(0, g->AddHook(new Track()));
  GateHook *hook = new Track();
  ASSERT_EQ(EEXIST, g->AddHook(hook));
  delete hook;
}

TEST_F(GateTest, HookPriority) {
  ASSERT_EQ(0, g->AddHook(new Track()));
  ASSERT_EQ(0, g->AddHook(new Tcpdump()));
  ASSERT_EQ(Track::kName, g->hooks()[0]->name());
}

TEST_F(GateTest, FindHook) {
  ASSERT_EQ(nullptr, g->FindHook(Track::kName));
  ASSERT_EQ(0, g->AddHook(new Track()));
  ASSERT_NE(nullptr, g->FindHook(Track::kName));
}

TEST_F(GateTest, RemoveHook) {
  ASSERT_EQ(0, g->AddHook(new Track()));
  g->RemoveHook(Track::kName);
  ASSERT_EQ(nullptr, g->FindHook(Track::kName));
}

TEST(HookTest, Track) {
  Track t;
  bess::PacketBatch b;
  b.set_cnt(32);
  t.ProcessBatch(&b);
  ASSERT_EQ(1, t.cnt());
  ASSERT_EQ(b.cnt(), t.pkts());
}

TEST_F(IOGateTest, OGate) {
  og->set_igate(ig);
  og->set_igate_idx(0);
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
