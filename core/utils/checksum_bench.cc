#include "checksum.h"

#include <rte_config.h>
#include <rte_ip.h>

#include <benchmark/benchmark.h>
#include <glog/logging.h>

#include "ether.h"
#include "random.h"

using namespace bess::utils;

class ChecksumFixture : public benchmark::Fixture {
 public:
  virtual void SetUp(benchmark::State &) {
    for (auto &t : buf_) {
      t = rd_.Get();
    }
  }

  void *get_buffer(uint16_t size) {
    CHECK_LE(size, sizeof(buf_));
    return buf_;
  }

  uint32_t GetRandom() { return rd_.Get(); }

 private:
  uint32_t buf_[2048];
  Random rd_;
};

// Benchmarks DPDK generic checksum
BENCHMARK_DEFINE_F(ChecksumFixture, BmGenericChecksumDpdk)
(benchmark::State &state) {
  size_t buf_len = state.range(0);

  while (state.KeepRunning()) {
    const void *buf = get_buffer(buf_len);

    // DPDK raw cksum does not return the negative of the sum
    // so we take the negative here
    benchmark::DoNotOptimize(~rte_raw_cksum(buf, buf_len));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(buf_len * state.iterations());
}

// Benchmarks BESS generic checksum
BENCHMARK_DEFINE_F(ChecksumFixture, BmGenericChecksumBess)
(benchmark::State &state) {
  size_t buf_len = state.range(0);

  while (state.KeepRunning()) {
    const void *buf = get_buffer(buf_len);

    benchmark::DoNotOptimize(CalculateGenericChecksum(buf, buf_len));
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

// Benchmarks DPDK IP checksum
BENCHMARK_DEFINE_F(ChecksumFixture, BmIpv4NoOptChecksumDpdk)
(benchmark::State &state) {
  char pkt[20] = {0};  // ipv4 header w/o options

  bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(pkt);

  ip->version = 4;
  ip->header_length = 5;
  ip->type_of_service = 0;
  ip->length = be16_t(40);
  ip->fragment_offset = be16_t(0);
  ip->ttl = 10;
  ip->protocol = 0x06;    // tcp
  ip->checksum = 0x0000;  // for dpdk

  while (state.KeepRunning()) {
    ip->src = be32_t(GetRandom());
    ip->dst = be32_t(GetRandom());

    benchmark::DoNotOptimize(
        rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip)));
  }

  state.SetItemsProcessed(state.iterations());
}

// Benchmarks BESS IP checksum
BENCHMARK_DEFINE_F(ChecksumFixture, BmIpv4NoOptChecksumBess)
(benchmark::State &state) {
  char pkt[20] = {0};  // ipv4 header w/o options

  bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(pkt);

  ip->version = 4;
  ip->header_length = 5;
  ip->type_of_service = 0;
  ip->length = be16_t(40);
  ip->fragment_offset = be16_t(0);
  ip->ttl = 10;
  ip->protocol = 0x06;    // tcp
  ip->checksum = 0x0000;  // for dpdk

  while (state.KeepRunning()) {
    ip->src = be32_t(GetRandom());
    ip->dst = be32_t(GetRandom());

    benchmark::DoNotOptimize(CalculateIpv4NoOptChecksum(*ip));
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmIpv4NoOptChecksumDpdk);
BENCHMARK_REGISTER_F(ChecksumFixture, BmIpv4NoOptChecksumBess);

// Benchmarks DPDK TCP checksum
BENCHMARK_DEFINE_F(ChecksumFixture, BmTcpChecksumDpdk)
(benchmark::State &state) {
  void *pkt;
  size_t buf_len = state.range(0);
  buf_len = (buf_len < 40) ? 40 : buf_len;

  while (state.KeepRunning()) {
    pkt = get_buffer(buf_len);

    bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(pkt);
    bess::utils::Tcp *tcp = reinterpret_cast<bess::utils::Tcp *>(ip + 1);

    ip->header_length = 5;
    ip->length = be16_t(buf_len);
    ip->protocol = 0x06;     // tcp
    tcp->checksum = 0x0000;  // for dpdk

    benchmark::DoNotOptimize(
        rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), tcp));
  }

  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(buf_len * state.iterations());
}

// Benchmarks BESS TCP checksum
BENCHMARK_DEFINE_F(ChecksumFixture, BmTcpChecksumBess)
(benchmark::State &state) {
  void *pkt;
  size_t buf_len = state.range(0);
  buf_len = (buf_len < 40) ? 40 : buf_len;

  while (state.KeepRunning()) {
    pkt = get_buffer(buf_len);

    bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(pkt);
    bess::utils::Tcp *tcp = reinterpret_cast<bess::utils::Tcp *>(ip + 1);

    ip->header_length = 5;
    ip->length = be16_t(buf_len);
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

// Benchmarks BESS incremental checksum update for 16-bit data update
BENCHMARK_DEFINE_F(ChecksumFixture, BmIncrementalUpdate16)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(Ethernet));
  bess::utils::Tcp *tcp = reinterpret_cast<bess::utils::Tcp *>(ip + 1);

  ip->header_length = 5;
  ip->length = be16_t(60);
  ip->protocol = 0x06;  // tcp

  uint16_t cksum = CalculateIpv4TcpChecksum(*ip, *tcp);

  while (state.KeepRunning()) {
    be16_t src_port_old = tcp->src_port;
    uint16_t cksum_update;

    tcp->src_port = be16_t(GetRandom());

    benchmark::DoNotOptimize(
        cksum_update = UpdateChecksum32(
            cksum, src_port_old.raw_value(), tcp->src_port.raw_value()));
    cksum = cksum_update;
  }

  state.SetItemsProcessed(state.iterations());
}

// Benchmarks BESS incremental checksum update for 32-bit data update
BENCHMARK_DEFINE_F(ChecksumFixture, BmIncrementalUpdate32)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(Ethernet));
  bess::utils::Tcp *tcp = reinterpret_cast<bess::utils::Tcp *>(ip + 1);

  ip->header_length = 5;
  ip->length = be16_t(60);
  ip->protocol = 0x06;  // tcp

  uint16_t cksum = CalculateIpv4TcpChecksum(*ip, *tcp);

  while (state.KeepRunning()) {
    be32_t src_ip_old = ip->src;
    uint16_t cksum_update;

    ip->src = be32_t(GetRandom());

    benchmark::DoNotOptimize(
        cksum_update = UpdateChecksum32(
            cksum, src_ip_old.raw_value(), ip->src.raw_value()));
    cksum = cksum_update;
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmIncrementalUpdate16);
BENCHMARK_REGISTER_F(ChecksumFixture, BmIncrementalUpdate32);

// Benchmarks DPDK Source IP/port update (Simulating NAT)
BENCHMARK_DEFINE_F(ChecksumFixture, BmSrcIpPortUpdateDpdk)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(Ethernet));
  bess::utils::Tcp *tcp = reinterpret_cast<bess::utils::Tcp *>(ip + 1);

  ip->header_length = 5;
  ip->length = be16_t(60);
  ip->protocol = 0x06;  // tcp

  while (state.KeepRunning()) {
    ip->src = be32_t(GetRandom());
    tcp->src_port = be16_t(GetRandom());

    // NAT simulation
    // - one update for ip checksum recalcuation
    // - two for tcp checksum
    ip->checksum = rte_ipv4_cksum(reinterpret_cast<const ipv4_hdr *>(ip));
    tcp->checksum =
        rte_ipv4_udptcp_cksum(reinterpret_cast<const ipv4_hdr *>(ip), tcp);
  }

  state.SetItemsProcessed(state.iterations());
}

// Benchmarks BESS Source IP/port update (Simulating NAT)
BENCHMARK_DEFINE_F(ChecksumFixture, BmSrcIpPortUpdateBess)
(benchmark::State &state) {
  void *pkt = get_buffer(60);  // min ethernet pkt size except FCS

  bess::utils::Ipv4 *ip = reinterpret_cast<bess::utils::Ipv4 *>(
      reinterpret_cast<uint8_t *>(pkt) + sizeof(Ethernet));
  bess::utils::Tcp *tcp = reinterpret_cast<bess::utils::Tcp *>(ip + 1);

  ip->header_length = 5;
  ip->length = be16_t(60);
  ip->protocol = 0x06;  // tcp

  ip->checksum = CalculateIpv4NoOptChecksum(*ip);
  tcp->checksum = CalculateIpv4TcpChecksum(*ip, *tcp);

  while (state.KeepRunning()) {
    be32_t src_ip_old = ip->src;
    be16_t src_port_old = tcp->src_port;

    ip->src = be32_t(GetRandom());
    tcp->src_port = be16_t(GetRandom());

    // NAT simulation
    // - one update (IP addr) for ip checksum recalcuation
    // - two updates (IP addr and TCP port) for tcp checksum
    uint32_t l3_inc =
        ChecksumIncrement32(src_ip_old.raw_value(), ip->src.raw_value());
    uint32_t l4_inc = l3_inc + ChecksumIncrement16(src_port_old.raw_value(),
                                                   tcp->src_port.raw_value());

    ip->checksum = UpdateChecksumWithIncrement(ip->checksum, l3_inc);
    tcp->checksum = UpdateChecksumWithIncrement(tcp->checksum, l4_inc);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(ChecksumFixture, BmSrcIpPortUpdateDpdk);
BENCHMARK_REGISTER_F(ChecksumFixture, BmSrcIpPortUpdateBess);

BENCHMARK_MAIN();
