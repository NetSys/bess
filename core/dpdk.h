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
