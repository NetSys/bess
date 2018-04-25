/* Copyright (c) 2018, Nefeli Networks, Inc. All rights reserved.
 * x86_64 BPF JIT code was adopted from FreeBSD 10 - Sangjin
 * Copyright (C) 2002-2003 NetGroup, Politecnico di Torino (Italy)
 * Copyright (C) 2005-2009 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
*/

#ifndef BESS_UTILS_BPF_H_
#define BESS_UTILS_BPF_H_

#include <cstring>
#include <pcap.h>
#include <string>
#include <sys/mman.h>

namespace bess {
namespace utils {

using bpf_filter_func_t = u_int (*)(u_char *, u_int, u_int);

struct Filter {
#ifdef __x86_64
  bpf_filter_func_t func;
  size_t mmap_size; // needed for munmap()
#else
  bpf_program il_code;
#endif
  int gate;
  int priority;    // higher number == higher priority
  std::string exp; // original filter expression string
};

#ifdef __x86_64
bpf_filter_func_t bpf_jit_compile(struct bpf_insn *prog, u_int nins,
                                  size_t *size);
#endif //__x86_64

} // namespace utils
} // namespace bess

#endif // BESS_UTILS_ARP_H_
