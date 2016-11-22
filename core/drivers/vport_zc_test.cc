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
 public:
  static bool dpdk_inited;

 protected:
  virtual void SetUp() {
    if (!dpdk_inited) {
      init_dpdk("vport_zc_test", 2048, 0, true);
      dpdk_inited = true;
    }
    bess::pb::EmptyArg arg;
    ADD_DRIVER(ZeroCopyVPort, "zcvport",
               "zero copy virtual port for trusted user apps")
    ASSERT_TRUE(__driver__ZeroCopyVPort);
    const PortBuilder &builder =
        PortBuilder::all_port_builders().find("ZeroCopyVPort")->second;
    port = reinterpret_cast<ZeroCopyVPort *>(builder.CreatePort("p0"));
    ASSERT_NE(nullptr, port);
    port->num_queues[PACKET_DIR_INC] = 1;
    port->num_queues[PACKET_DIR_OUT] = 1;
    ASSERT_EQ(0, port->Init(arg).err());
  }

  virtual void TearDown() {
    PortBuilder::all_port_builders_holder(true);
    PortBuilder::all_ports_.clear();
    if (port) {
      port->DeInit();
      delete port;
    }
  }
  ZeroCopyVPort *port;
};

bool ZeroCopyVPortTest::dpdk_inited = false;

TEST_F(ZeroCopyVPortTest, Send) {
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
  cnt = port->SendPackets(0, tx_batch.pkts(), tx_batch.cnt());
  ASSERT_EQ(tx_batch.cnt(), cnt);
  bess::Packet::Free(&tx_batch);
}

TEST_F(ZeroCopyVPortTest, Recv) {
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
  llring_enqueue_bulk(port->inc_qs_[0],
                      reinterpret_cast<void **>(tx_batch.pkts()),
                      tx_batch.cnt());

  rx_batch.set_cnt(port->RecvPackets(0, rx_batch.pkts(), tx_batch.cnt()));
  ASSERT_EQ(tx_batch.cnt(), rx_batch.cnt());
  tx_batch.clear();
  bess::Packet::Free(&rx_batch);
}
