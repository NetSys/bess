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

#include "vport_zc.h"

#include <sys/stat.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "../dpdk.h"
#include "../kmod/llring.h"
#include "../message.h"
#include "../packet.h"
#include "../pktbatch.h"
#include "../port.h"

class ZeroCopyVPortTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    port_ = nullptr;

    if (!dpdk_inited_) {
      if (geteuid() == 0) {
        init_dpdk("vport_zc_test", 1024, 0, true);
        dpdk_inited_ = true;
      } else {
        LOG(INFO) << "This test requires root privileges. Skipping...";
        return;
      }
    }

    bess::pb::EmptyArg arg;
    ADD_DRIVER(ZeroCopyVPort, "zcvport",
               "zero copy virtual port for trusted user apps")
    ASSERT_TRUE(__driver__ZeroCopyVPort);
    const PortBuilder &builder =
        PortBuilder::all_port_builders().find("ZeroCopyVPort")->second;
    port_ = reinterpret_cast<ZeroCopyVPort *>(builder.CreatePort("p0"));
    ASSERT_NE(nullptr, port_);
    port_->num_queues[PACKET_DIR_INC] = 1;
    port_->num_queues[PACKET_DIR_OUT] = 1;
    ASSERT_EQ(0, port_->Init(arg).error().code());
  }

  virtual void TearDown() {
    if (!dpdk_inited_) {
      return;
    }

    PortBuilder::all_port_builders_holder(true);
    PortBuilder::all_ports_.clear();
    if (port_) {
      port_->DeInit();
      delete port_;
    }
  }

  ZeroCopyVPort *port_;
  static bool dpdk_inited_;
};

bool ZeroCopyVPortTest::dpdk_inited_ = false;

TEST_F(ZeroCopyVPortTest, Send) {
  if (!dpdk_inited_) {
    return;
  }

  int cnt;
  bess::PacketBatch tx_batch;
  bess::Packet pkts[bess::PacketBatch::kMaxBurst];
  tx_batch.clear();
  for (size_t i = 0; i < bess::PacketBatch::kMaxBurst; i++) {
    bess::Packet *pkt = &pkts[i];

    // this fake packet must not be freed
    pkt->set_refcnt(2);

    // not chained
    pkt->set_next(nullptr);

    tx_batch.add(pkt);
  }
  cnt = port_->SendPackets(0, tx_batch.pkts(), tx_batch.cnt());
  ASSERT_EQ(tx_batch.cnt(), cnt);
  bess::Packet::Free(&tx_batch);
}

TEST_F(ZeroCopyVPortTest, Recv) {
  if (!dpdk_inited_) {
    return;
  }

  bess::PacketBatch tx_batch;
  bess::PacketBatch rx_batch;
  bess::Packet pkts[bess::PacketBatch::kMaxBurst];
  tx_batch.clear();
  for (size_t i = 0; i < bess::PacketBatch::kMaxBurst; i++) {
    bess::Packet *pkt = &pkts[i];

    // this fake packet must not be freed
    pkt->set_refcnt(2);

    // not chained
    pkt->set_next(nullptr);

    tx_batch.add(pkt);
  }

  // Packets arrived from somewhere
  llring_enqueue_bulk(port_->inc_qs_[0],
                      reinterpret_cast<void **>(tx_batch.pkts()),
                      tx_batch.cnt());

  rx_batch.set_cnt(port_->RecvPackets(0, rx_batch.pkts(), tx_batch.cnt()));
  ASSERT_EQ(tx_batch.cnt(), rx_batch.cnt());
  tx_batch.clear();
  bess::Packet::Free(&rx_batch);
}
