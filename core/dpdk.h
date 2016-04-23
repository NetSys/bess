#ifndef _DPDK_H_
#define _DPDK_H_

/* rte_version.h depends on these but has no #include :( */
#include <stdio.h>
#include <string.h>

#include <rte_version.h>

#ifndef RTE_VER_MAJOR
  #error DPDK version is not available
#else
  #define DPDK_VER(a,b,c) (((a << 16) | (b << 8) | (c)))
  #define DPDK DPDK_VER(RTE_VER_MAJOR,RTE_VER_MINOR,RTE_VER_PATCH_LEVEL)
#endif

void init_dpdk(char *prog_name, int mb_per_socket, int multi_instance);

#endif
