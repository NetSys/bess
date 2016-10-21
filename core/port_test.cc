// Unit tests for port and portbuilder routines.

#include "port.h"

#include <memory>

#include <gtest/gtest.h>

class DummyPort : public Port {
 public:
  DummyPort() : Port(), deinited_(nullptr) {}

  virtual void InitDriver() {
    initialized_ = true;
  }

  virtual void Deinit() {
    if (deinited_) *deinited_ = true;
  }

  void set_deinited(bool *val) {
    deinited_ = val;
  }

  static void set_initialized(bool val) {
    initialized_ = val;
  }

  static bool initialized() {
    return initialized_;
  }

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

     ASSERT_TRUE(PortBuilder::all_port_builders().empty());

     ADD_DRIVER(DummyPort, "dummy_port", "dummy help");
     ASSERT_TRUE(__driver__DummyPort);

     ASSERT_EQ(1, PortBuilder::all_port_builders().size());
     ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));
     dummy_port_builder = &PortBuilder::all_port_builders().find("DummyPort")->second;
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

// Checks that when we create a port via the established PortBuilder, the right
// Port object is returned.
TEST_F(PortTest, CreatePort) {
  std::unique_ptr<Port> p(dummy_port_builder->CreatePort("port1"));
  ASSERT_NE(nullptr, p.get());

  EXPECT_EQ("port1", p->name());
  EXPECT_EQ(dummy_port_builder, p->port_builder());
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

// Checks that adding a port puts it into the global port collection.
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
  PortBuilder::DestroyPort(p_fetched);
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
    ASSERT_FALSE(ret);

    it = it_next;
  }

  EXPECT_TRUE(PortBuilder::all_ports().empty());
}


// Checks that the default port name generator produces names that match the
// naming scheme.
TEST_F(PortTest, GenerateDefaultPortName) {
  // TODO(barath): Sangjin -- I don't quite know the naming scheme that is
  //               used right now, so we can add this test later.
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

// Checks that when we register a portclass, the global table of PortBuilders
// contains it.
TEST(PortBuilderTest, RegisterPortClassDirectCall) {
  ASSERT_TRUE(PortBuilder::all_port_builders().empty());

  PortBuilder::RegisterPortClass([]() { return new DummyPort(); }, "DummyPort", "dummy_port", "dummy help");

  ASSERT_EQ(1, PortBuilder::all_port_builders().size());
  ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));

  const PortBuilder &b = PortBuilder::all_port_builders().find("DummyPort")->second;
  EXPECT_EQ("DummyPort", b.class_name());
  EXPECT_EQ("dummy_port", b.name_template());
  EXPECT_EQ("dummy help", b.help_text());

  PortBuilder::all_port_builders_holder(true);
}

// Checks that when we register a portclass via the ADD_DRIVER macro, the global
// table of PortBuilders contains it.
TEST(PortBuilderTest, RegisterPortClassMacroCall) {
  ASSERT_TRUE(PortBuilder::all_port_builders().empty());

  ADD_DRIVER(DummyPort, "dummy_port", "dummy help");
  ASSERT_TRUE(__driver__DummyPort);

  ASSERT_EQ(1, PortBuilder::all_port_builders().size());
  ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));

  const PortBuilder &b = PortBuilder::all_port_builders().find("DummyPort")->second;
  EXPECT_EQ("DummyPort", b.class_name());
  EXPECT_EQ("dummy_port", b.name_template());
  EXPECT_EQ("dummy help", b.help_text());

  PortBuilder::all_port_builders_holder(true);
}

