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

// Unit tests for port and portbuilder routines.

#include "port.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

class DummyPort : public Port {
 public:
  DummyPort() : Port(), deinited_(nullptr) {}

  void InitDriver() override { initialized_ = true; }

  CommandResponse Init(const google::protobuf::Any &) {
    return CommandFailure(42);
  }

  void DeInit() override {
    if (deinited_)
      *deinited_ = true;
  }

  int RecvPackets(queue_t, bess::Packet **, int) override { return 0; }
  int SendPackets(queue_t, bess::Packet **, int) override { return 0; }

  void set_deinited(bool *val) { deinited_ = val; }

  static void set_initialized(bool val) { initialized_ = val; }

  static bool initialized() { return initialized_; }

 private:
  bool *deinited_;

  static bool initialized_;
};

bool DummyPort::initialized_ = false;

// A basic test framework for ports.  Sets up a single dummy PortBuilder that
// builds Ports of type DummyPort.
class PortTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    DummyPort::set_initialized(false);

    PortBuilder::all_port_builders_holder(true);
    ASSERT_TRUE(PortBuilder::all_port_builders().empty());

    ADD_DRIVER(DummyPort, "dummy_port", "dummy help");
    ASSERT_TRUE(__driver__DummyPort);

    ASSERT_EQ(1, PortBuilder::all_port_builders().size());
    ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));
    dummy_port_builder =
        &PortBuilder::all_port_builders().find("DummyPort")->second;
    EXPECT_EQ("DummyPort", dummy_port_builder->class_name());
    EXPECT_EQ("dummy_port", dummy_port_builder->name_template());
    EXPECT_EQ("dummy help", dummy_port_builder->help_text());
  }

  virtual void TearDown() {
    PortBuilder::all_port_builders_holder(true);
    PortBuilder::all_ports_.clear();
  }

  const PortBuilder *dummy_port_builder;
};

class PortBuilderTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    PortBuilder::all_port_builders_holder(true);
    ASSERT_TRUE(PortBuilder::all_port_builders().empty());
  }

  virtual void TearDown() { PortBuilder::all_port_builders_holder(true); }
};

// Checks that when we create a port via the established PortBuilder, the right
// Port object is returned.
TEST_F(PortTest, CreatePort) {
  std::unique_ptr<Port> p(dummy_port_builder->CreatePort("port1"));
  ASSERT_NE(nullptr, p.get());

  EXPECT_EQ("port1", p->name());
  EXPECT_EQ(dummy_port_builder, p->port_builder());

  bess::pb::EmptyArg arg_;
  google::protobuf::Any arg;
  arg.PackFrom(arg_);
  CommandResponse err = p->InitWithGenericArg(arg);
  EXPECT_EQ(42, err.error().code());
}

// Checks that adding a port puts it into the global port collection.
TEST_F(PortTest, AddPort) {
  std::unique_ptr<Port> p(dummy_port_builder->CreatePort("port1"));
  ASSERT_NE(nullptr, p.get());

  ASSERT_TRUE(PortBuilder::all_ports().empty());
  PortBuilder::AddPort(p.get());
  ASSERT_EQ(1, PortBuilder::all_ports().size());

  const auto &it = PortBuilder::all_ports().find("port1");
  ASSERT_NE(it, PortBuilder::all_ports().end());

  const Port *p_fetched = it->second;
  EXPECT_EQ("port1", p_fetched->name());
  EXPECT_EQ(dummy_port_builder, p_fetched->port_builder());
}

// Checks that we can get (empty) stats for a port.
TEST_F(PortTest, GetPortStats) {
  std::unique_ptr<Port> p(dummy_port_builder->CreatePort("port1"));
  ASSERT_NE(nullptr, p.get());

  ASSERT_TRUE(PortBuilder::all_ports().empty());
  PortBuilder::AddPort(p.get());
  ASSERT_EQ(1, PortBuilder::all_ports().size());

  const auto &it = PortBuilder::all_ports().find("port1");
  ASSERT_NE(it, PortBuilder::all_ports().end());

  Port::PortStats stats = it->second->GetPortStats();
  EXPECT_EQ(0, stats.inc.packets);
  EXPECT_EQ(0, stats.inc.dropped);
  EXPECT_EQ(0, stats.inc.bytes);
  EXPECT_EQ(0, stats.out.packets);
  EXPECT_EQ(0, stats.out.dropped);
  EXPECT_EQ(0, stats.out.bytes);
}

// Checks that we can acquire and release queues.
TEST_F(PortTest, AcquireAndReleaseQueues) {
  std::unique_ptr<Port> p(dummy_port_builder->CreatePort("port1"));
  p->num_queues[PACKET_DIR_INC] = 1;
  p->num_queues[PACKET_DIR_OUT] = 1;
  ASSERT_NE(nullptr, p.get());

  ASSERT_TRUE(PortBuilder::all_ports().empty());
  PortBuilder::AddPort(p.get());
  ASSERT_EQ(1, PortBuilder::all_ports().size());

  const auto &it = PortBuilder::all_ports().find("port1");
  ASSERT_NE(it, PortBuilder::all_ports().end());

  // Set up two dummy modules; this isn't safe, but the pointers shouldn't be
  // dereferenced by the called code so it should be fine for the test.
  struct module *m1 = (struct module *)1;
  struct module *m2 = (struct module *)2;

  // First don't specify a valid direction, shouldn't work.
  EXPECT_EQ(-EINVAL, p->AcquireQueues(m1, PACKET_DIRS, nullptr, 1));

  ASSERT_EQ(0, p->AcquireQueues(m1, PACKET_DIR_INC, nullptr, 1));
  EXPECT_EQ(-EBUSY, p->AcquireQueues(m2, PACKET_DIR_INC, nullptr, 1));
  p->ReleaseQueues(m1, PACKET_DIR_INC, nullptr, 1);
  EXPECT_EQ(0, p->AcquireQueues(m2, PACKET_DIR_INC, nullptr, 1));
  p->ReleaseQueues(m2, PACKET_DIR_INC, nullptr, 1);

  queue_t queues;
  memset(&queues, 0, sizeof(queues));

  ASSERT_EQ(0, p->AcquireQueues(m1, PACKET_DIR_INC, &queues, 1));
  EXPECT_EQ(-EBUSY, p->AcquireQueues(m2, PACKET_DIR_INC, &queues, 1));
  p->ReleaseQueues(m1, PACKET_DIR_INC, &queues, 1);
  EXPECT_EQ(0, p->AcquireQueues(m2, PACKET_DIR_INC, &queues, 1));
  p->ReleaseQueues(m2, PACKET_DIR_INC, &queues, 1);
}

// Checks that destroying a port works.
TEST_F(PortTest, DestroyPort) {
  Port *p = dummy_port_builder->CreatePort("port1");
  ASSERT_NE(nullptr, p);

  ASSERT_TRUE(PortBuilder::all_ports().empty());
  PortBuilder::AddPort(p);
  ASSERT_EQ(1, PortBuilder::all_ports().size());

  const auto &it = PortBuilder::all_ports().find("port1");
  ASSERT_NE(it, PortBuilder::all_ports().end());

  Port *p_fetched = it->second;
  EXPECT_EQ("port1", p_fetched->name());
  EXPECT_EQ(dummy_port_builder, p_fetched->port_builder());

  // Now destroy the port.
  bool deinited = false;
  static_cast<DummyPort *>(p_fetched)->set_deinited(&deinited);
  ASSERT_FALSE(deinited);
  int ret = PortBuilder::DestroyPort(p_fetched);
  ASSERT_EQ(0, ret) << "DestroyPort returned -EBUSY? " << (ret == -EBUSY);
  ASSERT_TRUE(deinited);

  EXPECT_TRUE(PortBuilder::all_ports().empty());
}

// Checks that the logic for destroying multiple (all) ports works right.
TEST_F(PortTest, DestroyAllPorts) {
  Port *p1 = dummy_port_builder->CreatePort("port1");
  Port *p2 = dummy_port_builder->CreatePort("port2");
  ASSERT_NE(nullptr, p1);
  ASSERT_NE(nullptr, p2);

  ASSERT_TRUE(PortBuilder::all_ports().empty());
  PortBuilder::AddPort(p1);
  ASSERT_EQ(1, PortBuilder::all_ports().size());
  PortBuilder::AddPort(p2);
  ASSERT_EQ(2, PortBuilder::all_ports().size());

  ASSERT_TRUE(PortBuilder::all_ports().count("port1"));
  ASSERT_TRUE(PortBuilder::all_ports().count("port2"));

  // Now destroy all ports; logic from snctl.cc.
  for (auto it = PortBuilder::all_ports().cbegin();
       it != PortBuilder::all_ports().end();) {
    auto it_next = std::next(it);
    Port *p = it->second;

    int ret = PortBuilder::DestroyPort(p);
    EXPECT_EQ(0, ret) << "DestroyPort returned -EBUSY? " << (ret == -EBUSY);

    it = it_next;
  }

  EXPECT_TRUE(PortBuilder::all_ports().empty());
}

// Checks that a port's driver is initialized when the port class is
// initialized.
TEST_F(PortTest, InitPortClass) {
  ASSERT_FALSE(DummyPort::initialized());

  PortBuilder *builder = const_cast<PortBuilder *>(dummy_port_builder);
  EXPECT_TRUE(builder->InitPortClass());
  EXPECT_TRUE(DummyPort::initialized());
  EXPECT_FALSE(builder->InitPortClass());
}

// Checks that a port's driver is initialized when all drivers are initialized.
TEST_F(PortTest, InitDrivers) {
  ASSERT_FALSE(DummyPort::initialized());

  PortBuilder::InitDrivers();

  EXPECT_TRUE(DummyPort::initialized());
  EXPECT_TRUE(dummy_port_builder->initialized());

  // Subsequent calls should fail.  (We can't check for this right now because
  // there is no return value.)
  PortBuilder::InitDrivers();
}

// Checks that when we register a portclass, the global table of PortBuilders
// contains it.
TEST_F(PortBuilderTest, RegisterPortClassDirectCall) {
  PortBuilder::RegisterPortClass([]() { return new DummyPort(); }, "DummyPort",
                                 "dummy_port", "dummy help",
                                 PORT_INIT_FUNC(&DummyPort::Init));

  ASSERT_EQ(1, PortBuilder::all_port_builders().size());
  ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));

  const PortBuilder &b =
      PortBuilder::all_port_builders().find("DummyPort")->second;
  EXPECT_EQ("DummyPort", b.class_name());
  EXPECT_EQ("dummy_port", b.name_template());
  EXPECT_EQ("dummy help", b.help_text());
}

// Checks that when we register a portclass via the ADD_DRIVER macro, the global
// table of PortBuilders contains it.
TEST_F(PortBuilderTest, RegisterPortClassMacroCall) {
  ADD_DRIVER(DummyPort, "dummy_port", "dummy help");
  ASSERT_TRUE(__driver__DummyPort);

  ASSERT_EQ(1, PortBuilder::all_port_builders().size());
  ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));

  const PortBuilder &b =
      PortBuilder::all_port_builders().find("DummyPort")->second;
  EXPECT_EQ("DummyPort", b.class_name());
  EXPECT_EQ("dummy_port", b.name_template());
  EXPECT_EQ("dummy help", b.help_text());
}

// Checks that we can generate a proper port name given a template or not.
TEST_F(PortBuilderTest, GenerateDefaultPortNameTemplate) {
  std::string name1 = PortBuilder::GenerateDefaultPortName("FooPort", "foo");
  EXPECT_EQ("foo0", name1);

  std::string name2 = PortBuilder::GenerateDefaultPortName("FooPort", "");
  EXPECT_EQ("foo_port0", name2);

  std::string name3 = PortBuilder::GenerateDefaultPortName("FooABCPort", "");
  EXPECT_EQ("foo_abcport0", name3);
}
