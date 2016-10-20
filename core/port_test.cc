// Unit tests for port and portbuilder routines.

#include "port.h"

#include <memory>

#include <gtest/gtest.h>

class DummyPort : public Port {
  virtual void InitDriver() {
    initialized = true;
  }

  bool initialized = false;
};

class PortTest : public ::testing::Test {
 protected:
   virtual void SetUp() {
     p = new DummyPort();
   }

   virtual void TearDown() {
     delete p;

     PortBuilder::all_port_builders_holder(true);
   }

   DummyPort *p;
};

TEST_F(PortTest, RegisterPortClassDirectCall) {
  ASSERT_TRUE(PortBuilder::all_port_builders().empty());

  PortBuilder::RegisterPortClass([]() { return new DummyPort(); }, "DummyPort", "dummy_port", "dummy help");

  ASSERT_EQ(1, PortBuilder::all_port_builders().size());
  ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));

  const PortBuilder &b = PortBuilder::all_port_builders().find("DummyPort")->second;
  EXPECT_EQ("DummyPort", b.class_name());
  EXPECT_EQ("dummy_port", b.name_template());
  EXPECT_EQ("dummy help", b.help_text());
}

TEST_F(PortTest, RegisterPortClassMacroCall) {
  ASSERT_TRUE(PortBuilder::all_port_builders().empty());

  ADD_DRIVER(DummyPort, "dummy_port", "dummy help");
  ASSERT_TRUE(__driver__DummyPort);

  ASSERT_EQ(1, PortBuilder::all_port_builders().size());
  ASSERT_EQ(1, PortBuilder::all_port_builders().count("DummyPort"));

  const PortBuilder &b = PortBuilder::all_port_builders().find("DummyPort")->second;
  EXPECT_EQ("DummyPort", b.class_name());
  EXPECT_EQ("dummy_port", b.name_template());
  EXPECT_EQ("dummy help", b.help_text());
}


