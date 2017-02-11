#include "checksum.h"

#include <rte_config.h>
#include <rte_ip.h>

#include <benchmark/benchmark.h>

#include "ether.h"
#include "random.h"

using namespace bess::utils;

class ChecksumFixture : public benchmark::Fixture {
 public:
  virtual void SetUp(benchmark::State &) {
    for (size_t i = 0; i < sizeof(buf_); i += sizeof(uint32_t)) {
      *reinterpret_cast<uint32_t *>(buf_ + i) = rd_.Get();
    }
  }

  void *get_buffer(uint16_t size) {
    CHECK(size <= sizeof(buf_));
    return buf_;
  }

  uint32_t GetRandom() { return rd_.Get(); }

 private:
  char buf_[2048];
  Random rd_;
};

BENCHMARK_DEFINE_F(ChecksumFixture, BmGenericChecksumDpdk)
(benchmark::State &state) {
  size_t buf_len = state.range(0);
  void *buf;

  while (state.KeepRunning()) {
    buf = get_buffer(buf_len);

    // take the complement for dpdk raw sum
    benchmark::DoNotOptimize(~rte_raw_cksum(buf, buf_len));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(buf_len * state.iterations());
}

BENCHMARK_DEFINE_F(ChecksumFixture, BmGenericChecksumBess)
(benchmark::State &state) {
  size_t buf_len = state.range(0);
  void *buf;

  while (state.KeepRunning()) {
    buf = get_buffer(buf_len);

    benchmark::DoNotOptimize(
        CalculateGenericChecksum(reinterpret_cast<const void *>(buf), buf_len));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(buf_len * state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmGenericChecksumDpdk)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(2048);
BENCHMARK_REGISTER_F(ChecksumFixture, BmGenericChecksumBess)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(2048);

BENCHMARK_DEFINE_F(ChecksumFixture, BmIpv4NoOptChecksumDpdk)
(benchmark::State &state) {
  char pkt[20] = {0};  // ipv4 header w/o options

  bess::utils::Ipv4Header *ip =
      reinterpret_cast<bess::utils::Ipv4Header *>(pkt);

  ip->version = 4;
  ip->header_length = 5;
  ip->type_of_service = 0;
  ip->length = htons(40);
  ip->fragment_offset = 0;
  ip->ttl = 10;
  ip->protocol = 0x06;    // tcp
  ip->checksum = 0x0000;  // for dpdk

  while (state.KeepRunning()) {
    ip->src = GetRandom();
    ip->dst = GetRandom();

    benchmark::DoNotOptimize(
        rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip)));
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(ChecksumFixture, BmIpv4NoOptChecksumBess)
(benchmark::State &state) {
  char pkt[20] = {0};  // ipv4 header w/o options

  bess::utils::Ipv4Header *ip =
      reinterpret_cast<bess::utils::Ipv4Header *>(pkt);

  ip->version = 4;
  ip->header_length = 5;
  ip->type_of_service = 0;
  ip->length = htons(40);
  ip->fragment_offset = 0;
  ip->ttl = 10;
  ip->protocol = 0x06;    // tcp
  ip->checksum = 0x0000;  // for dpdk

  while (state.KeepRunning()) {
    ip->src = GetRandom();
    ip->dst = GetRandom();

    benchmark::DoNotOptimize(CalculateIpv4NoOptChecksum(*ip));
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmIpv4NoOptChecksumDpdk);
BENCHMARK_REGISTER_F(ChecksumFixture, BmIpv4NoOptChecksumBess);

BENCHMARK_DEFINE_F(ChecksumFixture, BmTcpChecksumDpdk)
(benchmark::State &state) {
  void *pkt;
  size_t buf_len = state.range(0);
  buf_len = (buf_len < 40) ? 40 : buf_len;

  while (state.KeepRunning()) {
    pkt = get_buffer(buf_len);

    bess::utils::Ipv4Header *ip =
        reinterpret_cast<bess::utils::Ipv4Header *>(pkt);
    bess::utils::TcpHeader *tcp =
        reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

    ip->header_length = 5;
    ip->length = htons(buf_len);
    ip->protocol = 0x06;     // tcp
    tcp->checksum = 0x0000;  // for dpdk

    benchmark::DoNotOptimize(
        rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), tcp));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(buf_len * state.iterations());
}

BENCHMARK_DEFINE_F(ChecksumFixture, BmTcpChecksumBess)
(benchmark::State &state) {
  void *pkt;
  size_t buf_len = state.range(0);
  buf_len = (buf_len < 40) ? 40 : buf_len;

  while (state.KeepRunning()) {
    pkt = get_buffer(buf_len);

    bess::utils::Ipv4Header *ip =
        reinterpret_cast<bess::utils::Ipv4Header *>(pkt);
    bess::utils::TcpHeader *tcp =
        reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

    ip->header_length = 5;
    ip->length = htons(buf_len);
    ip->protocol = 0x06;     // tcp
    tcp->checksum = 0x0000;  // for dpdk

    benchmark::DoNotOptimize(CalculateIpv4TcpChecksum(*ip, *tcp));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(buf_len * state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmTcpChecksumDpdk)
    ->Arg(60)
    ->Arg(787)
    ->Arg(1514);
BENCHMARK_REGISTER_F(ChecksumFixture, BmTcpChecksumBess)
    ->Arg(60)
    ->Arg(787)
    ->Arg(1514);

BENCHMARK_DEFINE_F(ChecksumFixture, BmIncrementalUpdate16)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4Header *ip = reinterpret_cast<bess::utils::Ipv4Header *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(EthHeader));
  bess::utils::TcpHeader *tcp =
      reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

  ip->header_length = 5;
  ip->length = htons(60);
  ip->protocol = 0x06;  // tcp

  uint16_t cksum = CalculateIpv4TcpChecksum(*ip, *tcp);

  while (state.KeepRunning()) {
    uint16_t src_port_old = tcp->src_port;
    uint16_t cksum_update;

    tcp->src_port = GetRandom();

    benchmark::DoNotOptimize(cksum_update = CalculateChecksumIncrementalUpdate(
                                 cksum, src_port_old, tcp->src_port));
    cksum = cksum_update;
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(ChecksumFixture, BmIncrementalUpdate32)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4Header *ip = reinterpret_cast<bess::utils::Ipv4Header *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(EthHeader));
  bess::utils::TcpHeader *tcp =
      reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

  ip->header_length = 5;
  ip->length = htons(60);
  ip->protocol = 0x06;  // tcp

  uint16_t cksum = CalculateIpv4TcpChecksum(*ip, *tcp);

  while (state.KeepRunning()) {
    uint32_t src_ip_old = ip->src;
    uint16_t cksum_update;

    ip->src = GetRandom();

    benchmark::DoNotOptimize(cksum_update = CalculateChecksumIncrementalUpdate(
                                 cksum, src_ip_old, ip->src));
    cksum = cksum_update;
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmIncrementalUpdate16);
BENCHMARK_REGISTER_F(ChecksumFixture, BmIncrementalUpdate32);

BENCHMARK_DEFINE_F(ChecksumFixture, BmSrcIpPortUpdateDpdk)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4Header *ip = reinterpret_cast<bess::utils::Ipv4Header *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(EthHeader));
  bess::utils::TcpHeader *tcp =
      reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

  ip->header_length = 5;
  ip->length = htons(60);
  ip->protocol = 0x06;  // tcp

  while (state.KeepRunning()) {
    ip->src = GetRandom();
    tcp->src_port = GetRandom();

    // NAT simulation
    // - one update for ip checksum recalcuation
    // - two for tcp checksum
    benchmark::DoNotOptimize(
        ip->checksum = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip)));
    benchmark::DoNotOptimize(tcp->checksum = rte_ipv4_udptcp_cksum(
                                 reinterpret_cast<const ipv4_hdr *>(ip), tcp));
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_DEFINE_F(ChecksumFixture, BmSrcIpPortUpdateBess)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4Header *ip = reinterpret_cast<bess::utils::Ipv4Header *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(EthHeader));
  bess::utils::TcpHeader *tcp =
      reinterpret_cast<bess::utils::TcpHeader *>(ip + 1);

  ip->header_length = 5;
  ip->length = htons(60);
  ip->protocol = 0x06;  // tcp

  ip->checksum = CalculateIpv4NoOptChecksum(*ip);
  tcp->checksum = CalculateIpv4TcpChecksum(*ip, *tcp);

  while (state.KeepRunning()) {
    uint32_t src_ip_old = ip->src;
    uint16_t src_port_old = tcp->src_port;

    ip->src = GetRandom();
    tcp->src_port = GetRandom();

    // NAT simulation
    // - one update for ip checksum recalcuation
    // - two for tcp checksum
    benchmark::DoNotOptimize(ip->checksum = CalculateChecksumIncrementalUpdate(
                                 ip->checksum, src_ip_old, ip->src));
    benchmark::DoNotOptimize(tcp->checksum = CalculateChecksumIncrementalUpdate(
                                 tcp->checksum, src_ip_old, ip->src));
    benchmark::DoNotOptimize(tcp->checksum = CalculateChecksumIncrementalUpdate(
                                 tcp->checksum, src_port_old, tcp->src_port));
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmSrcIpPortUpdateDpdk);
BENCHMARK_REGISTER_F(ChecksumFixture, BmSrcIpPortUpdateBess);

BENCHMARK_MAIN();
