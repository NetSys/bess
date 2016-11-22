#ifndef BESS_DPDK_H_
#define BESS_DPDK_H_

/* rte_version.h depends on this but has no #include :( */
#include <cstdio>
#include <string>

#include <rte_version.h>

#define DPDK_VER_NUM(a, b, c) (((a << 16) | (b << 8) | (c)))

/* for DPDK 16.04 or newer */
#ifdef RTE_VER_YEAR
#define DPDK_VER DPDK_VER_NUM(RTE_VER_YEAR, RTE_VER_MONTH, RTE_VER_MINOR)
#endif

/* for DPDK 2.2 or older */
#ifdef RTE_VER_MAJOR
#define DPDK_VER DPDK_VER_NUM(RTE_VER_MAJOR, RTE_VER_MINOR, RTE_VER_PATCH_LEVEL)
#endif

#ifndef DPDK_VER
#error DPDK version is not available
#endif

void init_dpdk(const ::std::string &prog_name, int mb_per_socket,
               int multi_instance, bool no_huge);

#endif  // BESS_DPDK_H_
