/* x86_64 BPF JIT code was adopted from FreeBSD 10 - Sangjin */

/*-
 * Copyright (C) 2002-2003 NetGroup, Politecnico di Torino (Italy)
 * Copyright (C) 2005-2009 Jung-uk Kim <jkim@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bpf.h"

#include <pcap.h>
#include <sys/mman.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

/*
 * Registers
 */
#define RAX 0
#define RCX 1
#define RDX 2
#define RBX 3
#define RSP 4
#define RBP 5
#define RSI 6
#define RDI 7
#define R8 0
#define R9 1
#define R10 2
#define R11 3
#define R12 4
#define R13 5
#define R14 6
#define R15 7

#define EAX 0
#define ECX 1
#define EDX 2
#define EBX 3
#define ESP 4
#define EBP 5
#define ESI 6
#define EDI 7
#define R8D 0
#define R9D 1
#define R10D 2
#define R11D 3
#define R12D 4
#define R13D 5
#define R14D 6
#define R15D 7

#define AX 0
#define CX 1
#define DX 2
#define BX 3
#define SP 4
#define BP 5
#define SI 6
#define DI 7

#define AL 0
#define CL 1
#define DL 2
#define BL 3

/* Optimization flags */
#define BPF_JIT_FRET 0x01
#define BPF_JIT_FPKT 0x02
#define BPF_JIT_FMEM 0x04
#define BPF_JIT_FJMP 0x08
#define BPF_JIT_FLEN 0x10

#define BPF_JIT_FLAG_ALL \
  (BPF_JIT_FPKT | BPF_JIT_FMEM | BPF_JIT_FJMP | BPF_JIT_FLEN)

/* A stream of native binary code */
typedef struct bpf_bin_stream {
  /* Current native instruction pointer. */
  int cur_ip;

  /*
   * Current BPF instruction pointer, i.e. position in
   * the BPF program reached by the jitter.
   */
  int bpf_pc;

  /* Instruction buffer, contains the generated native code. */
  char *ibuf;

  /* Jumps reference table. */
  u_int *refs;
} bpf_bin_stream;

/*
 * Prototype of the emit functions.
 *
 * Different emit functions are used to create the reference table and
 * to generate the actual filtering code. This allows to have simpler
 * instruction macros.
 * The first parameter is the stream that will receive the data.
 * The second one is a variable containing the data.
 * The third one is the length, that can be 1, 2, or 4 since it is possible
 * to emit a byte, a short, or a word at a time.
 */
typedef void (*emit_func)(bpf_bin_stream *stream, u_int value, u_int n);

/*
 * Native instruction macros
 */

/* movl i32,r32 */
#define MOVid(i32, r32)                                    \
  do {                                                     \
    emitm(&stream, (11 << 4) | (1 << 3) | (r32 & 0x7), 1); \
    emitm(&stream, i32, 4);                                \
  } while (0)

/* movq i64,r64 */
#define MOViq(i64, r64)                                    \
  do {                                                     \
    emitm(&stream, 0x48, 1);                               \
    emitm(&stream, (11 << 4) | (1 << 3) | (r64 & 0x7), 1); \
    emitm(&stream, i64, 4);                                \
    emitm(&stream, (i64 >> 32), 4);                        \
  } while (0)

/* movl sr32,dr32 */
#define MOVrd(sr32, dr32)                                             \
  do {                                                                \
    emitm(&stream, 0x89, 1);                                          \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* movl sr32,dr32 (dr32 = %r8-15d) */
#define MOVrd2(sr32, dr32)                                            \
  do {                                                                \
    emitm(&stream, 0x8941, 2);                                        \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* movl sr32,dr32 (sr32 = %r8-15d) */
#define MOVrd3(sr32, dr32)                                            \
  do {                                                                \
    emitm(&stream, 0x8944, 2);                                        \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* movq sr64,dr64 */
#define MOVrq(sr64, dr64)                                             \
  do {                                                                \
    emitm(&stream, 0x8948, 2);                                        \
    emitm(&stream, (3 << 6) | ((sr64 & 0x7) << 3) | (dr64 & 0x7), 1); \
  } while (0)

/* movq sr64,dr64 (dr64 = %r8-15) */
#define MOVrq2(sr64, dr64)                                            \
  do {                                                                \
    emitm(&stream, 0x8949, 2);                                        \
    emitm(&stream, (3 << 6) | ((sr64 & 0x7) << 3) | (dr64 & 0x7), 1); \
  } while (0)

/* movq sr64,dr64 (sr64 = %r8-15) */
#define MOVrq3(sr64, dr64)                                            \
  do {                                                                \
    emitm(&stream, 0x894c, 2);                                        \
    emitm(&stream, (3 << 6) | ((sr64 & 0x7) << 3) | (dr64 & 0x7), 1); \
  } while (0)

/* movl (sr64,or64,1),dr32 */
#define MOVobd(sr64, or64, dr32)                           \
  do {                                                     \
    emitm(&stream, 0x8b, 1);                               \
    emitm(&stream, ((dr32 & 0x7) << 3) | 4, 1);            \
    emitm(&stream, ((or64 & 0x7) << 3) | (sr64 & 0x7), 1); \
  } while (0)

/* movw (sr64,or64,1),dr16 */
#define MOVobw(sr64, or64, dr16)                           \
  do {                                                     \
    emitm(&stream, 0x8b66, 2);                             \
    emitm(&stream, ((dr16 & 0x7) << 3) | 4, 1);            \
    emitm(&stream, ((or64 & 0x7) << 3) | (sr64 & 0x7), 1); \
  } while (0)

/* movb (sr64,or64,1),dr8 */
#define MOVobb(sr64, or64, dr8)                            \
  do {                                                     \
    emitm(&stream, 0x8a, 1);                               \
    emitm(&stream, ((dr8 & 0x7) << 3) | 4, 1);             \
    emitm(&stream, ((or64 & 0x7) << 3) | (sr64 & 0x7), 1); \
  } while (0)

/* movl sr32,(dr64,or64,1) */
#define MOVomd(sr32, dr64, or64)                           \
  do {                                                     \
    emitm(&stream, 0x89, 1);                               \
    emitm(&stream, ((sr32 & 0x7) << 3) | 4, 1);            \
    emitm(&stream, ((or64 & 0x7) << 3) | (dr64 & 0x7), 1); \
  } while (0)

/* bswapl dr32 */
#define BSWAP(dr32)                        \
  do {                                     \
    emitm(&stream, 0xf, 1);                \
    emitm(&stream, (0x19 << 3) | dr32, 1); \
  } while (0)

/* xchgb %al,%ah */
#define SWAP_AX()              \
  do {                         \
    emitm(&stream, 0xc486, 2); \
  } while (0)

/* pushq r64 */
#define PUSH(r64)                                         \
  do {                                                    \
    emitm(&stream, (5 << 4) | (0 << 3) | (r64 & 0x7), 1); \
  } while (0)

/* leaveq */
#define LEAVE()              \
  do {                       \
    emitm(&stream, 0xc9, 1); \
  } while (0)

/* retq */
#define RET()                \
  do {                       \
    emitm(&stream, 0xc3, 1); \
  } while (0)

/* addl sr32,dr32 */
#define ADDrd(sr32, dr32)                                             \
  do {                                                                \
    emitm(&stream, 0x01, 1);                                          \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* addl i32,%eax */
#define ADD_EAXi(i32)        \
  do {                       \
    emitm(&stream, 0x05, 1); \
    emitm(&stream, i32, 4);  \
  } while (0)

/* addl i8,r32 */
#define ADDib(i8, r32)                  \
  do {                                  \
    emitm(&stream, 0x83, 1);            \
    emitm(&stream, (24 << 3) | r32, 1); \
    emitm(&stream, i8, 1);              \
  } while (0)

/* subl sr32,dr32 */
#define SUBrd(sr32, dr32)                                             \
  do {                                                                \
    emitm(&stream, 0x29, 1);                                          \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* subl i32,%eax */
#define SUB_EAXi(i32)        \
  do {                       \
    emitm(&stream, 0x2d, 1); \
    emitm(&stream, i32, 4);  \
  } while (0)

/* subq i8,r64 */
#define SUBib(i8, r64)                          \
  do {                                          \
    emitm(&stream, 0x8348, 2);                  \
    emitm(&stream, (29 << 3) | (r64 & 0x7), 1); \
    emitm(&stream, i8, 1);                      \
  } while (0)

/* mull r32 */
#define MULrd(r32)                             \
  do {                                         \
    emitm(&stream, 0xf7, 1);                   \
    emitm(&stream, (7 << 5) | (r32 & 0x7), 1); \
  } while (0)

/* divl r32 */
#define DIVrd(r32)                              \
  do {                                          \
    emitm(&stream, 0xf7, 1);                    \
    emitm(&stream, (15 << 4) | (r32 & 0x7), 1); \
  } while (0)

/* andb i8,r8 */
#define ANDib(i8, r8)                   \
  do {                                  \
    if (r8 == AL) {                     \
      emitm(&stream, 0x24, 1);          \
    } else {                            \
      emitm(&stream, 0x80, 1);          \
      emitm(&stream, (7 << 5) | r8, 1); \
    }                                   \
    emitm(&stream, i8, 1);              \
  } while (0)

/* andl i32,r32 */
#define ANDid(i32, r32)                  \
  do {                                   \
    if (r32 == EAX) {                    \
      emitm(&stream, 0x25, 1);           \
    } else {                             \
      emitm(&stream, 0x81, 1);           \
      emitm(&stream, (7 << 5) | r32, 1); \
    }                                    \
    emitm(&stream, i32, 4);              \
  } while (0)

/* andl sr32,dr32 */
#define ANDrd(sr32, dr32)                                             \
  do {                                                                \
    emitm(&stream, 0x21, 1);                                          \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* testl i32,r32 */
#define TESTid(i32, r32)                 \
  do {                                   \
    if (r32 == EAX) {                    \
      emitm(&stream, 0xa9, 1);           \
    } else {                             \
      emitm(&stream, 0xf7, 1);           \
      emitm(&stream, (3 << 6) | r32, 1); \
    }                                    \
    emitm(&stream, i32, 4);              \
  } while (0)

/* testl sr32,dr32 */
#define TESTrd(sr32, dr32)                                            \
  do {                                                                \
    emitm(&stream, 0x85, 1);                                          \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* orl sr32,dr32 */
#define ORrd(sr32, dr32)                                              \
  do {                                                                \
    emitm(&stream, 0x09, 1);                                          \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* orl i32,r32 */
#define ORid(i32, r32)                    \
  do {                                    \
    if (r32 == EAX) {                     \
      emitm(&stream, 0x0d, 1);            \
    } else {                              \
      emitm(&stream, 0x81, 1);            \
      emitm(&stream, (25 << 3) | r32, 1); \
    }                                     \
    emitm(&stream, i32, 4);               \
  } while (0)

/* shll i8,r32 */
#define SHLib(i8, r32)                         \
  do {                                         \
    emitm(&stream, 0xc1, 1);                   \
    emitm(&stream, (7 << 5) | (r32 & 0x7), 1); \
    emitm(&stream, i8, 1);                     \
  } while (0)

/* shll %cl,dr32 */
#define SHL_CLrb(dr32)                          \
  do {                                          \
    emitm(&stream, 0xd3, 1);                    \
    emitm(&stream, (7 << 5) | (dr32 & 0x7), 1); \
  } while (0)

/* shrl i8,r32 */
#define SHRib(i8, r32)                          \
  do {                                          \
    emitm(&stream, 0xc1, 1);                    \
    emitm(&stream, (29 << 3) | (r32 & 0x7), 1); \
    emitm(&stream, i8, 1);                      \
  } while (0)

/* shrl %cl,dr32 */
#define SHR_CLrb(dr32)                           \
  do {                                           \
    emitm(&stream, 0xd3, 1);                     \
    emitm(&stream, (29 << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* negl r32 */
#define NEGd(r32)                               \
  do {                                          \
    emitm(&stream, 0xf7, 1);                    \
    emitm(&stream, (27 << 3) | (r32 & 0x7), 1); \
  } while (0)

/* cmpl sr32,dr32 */
#define CMPrd(sr32, dr32)                                             \
  do {                                                                \
    emitm(&stream, 0x39, 1);                                          \
    emitm(&stream, (3 << 6) | ((sr32 & 0x7) << 3) | (dr32 & 0x7), 1); \
  } while (0)

/* cmpl i32,dr32 */
#define CMPid(i32, dr32)                             \
  do {                                               \
    if (dr32 == EAX) {                               \
      emitm(&stream, 0x3d, 1);                       \
      emitm(&stream, i32, 4);                        \
    } else {                                         \
      emitm(&stream, 0x81, 1);                       \
      emitm(&stream, (0x1f << 3) | (dr32 & 0x7), 1); \
      emitm(&stream, i32, 4);                        \
    }                                                \
  } while (0)

/* jb off8 */
#define JBb(off8)            \
  do {                       \
    emitm(&stream, 0x72, 1); \
    emitm(&stream, off8, 1); \
  } while (0)

/* jae off8 */
#define JAEb(off8)           \
  do {                       \
    emitm(&stream, 0x73, 1); \
    emitm(&stream, off8, 1); \
  } while (0)

/* jne off8 */
#define JNEb(off8)           \
  do {                       \
    emitm(&stream, 0x75, 1); \
    emitm(&stream, off8, 1); \
  } while (0)

/* ja off8 */
#define JAb(off8)            \
  do {                       \
    emitm(&stream, 0x77, 1); \
    emitm(&stream, off8, 1); \
  } while (0)

/* jmp off32 */
#define JMP(off32)            \
  do {                        \
    emitm(&stream, 0xe9, 1);  \
    emitm(&stream, off32, 4); \
  } while (0)

/* xorl r32,r32 */
#define ZEROrd(r32)                                                 \
  do {                                                              \
    emitm(&stream, 0x31, 1);                                        \
    emitm(&stream, (3 << 6) | ((r32 & 0x7) << 3) | (r32 & 0x7), 1); \
  } while (0)

/*
 * Conditional long jumps
 */
#define JB 0x82
#define JAE 0x83
#define JE 0x84
#define JNE 0x85
#define JBE 0x86
#define JA 0x87

#define JCC(t, f)                                                              \
  do {                                                                         \
    if (ins->jt != 0 && ins->jf != 0) {                                        \
      /* 5 is the size of the following jmp */                                 \
      emitm(&stream, ((t) << 8) | 0x0f, 2);                                    \
      emitm(&stream, stream.refs[stream.bpf_pc + ins->jt] -                    \
                         stream.refs[stream.bpf_pc] + 5,                       \
            4);                                                                \
      JMP(stream.refs[stream.bpf_pc + ins->jf] - stream.refs[stream.bpf_pc]);  \
    } else if (ins->jt != 0) {                                                 \
      emitm(&stream, ((t) << 8) | 0x0f, 2);                                    \
      emitm(&stream,                                                           \
            stream.refs[stream.bpf_pc + ins->jt] - stream.refs[stream.bpf_pc], \
            4);                                                                \
    } else {                                                                   \
      emitm(&stream, ((f) << 8) | 0x0f, 2);                                    \
      emitm(&stream,                                                           \
            stream.refs[stream.bpf_pc + ins->jf] - stream.refs[stream.bpf_pc], \
            4);                                                                \
    }                                                                          \
  } while (0)

#define JUMP(off)                                                           \
  do {                                                                      \
    if ((off) != 0)                                                         \
      JMP(stream.refs[stream.bpf_pc + (off)] - stream.refs[stream.bpf_pc]); \
  } while (0)

/*
 * Emit routine to update the jump table.
 */
static void emit_length(bpf_bin_stream *stream, u_int, u_int len) {
  if (stream->refs != nullptr)
    (stream->refs)[stream->bpf_pc] += len;
  stream->cur_ip += len;
}

/*
 * Emit routine to output the actual binary code.
 */
static void emit_code(bpf_bin_stream *stream, u_int value, u_int len) {
  switch (len) {
    case 1:
      stream->ibuf[stream->cur_ip] = (u_char)value;
      stream->cur_ip++;
      break;

    case 2:
      *(reinterpret_cast<u_short *>(stream->ibuf + stream->cur_ip)) =
          (u_short)value;
      stream->cur_ip += 2;
      break;

    case 4:
      *(reinterpret_cast<u_int *>(stream->ibuf + stream->cur_ip)) = value;
      stream->cur_ip += 4;
      break;
  }

  return;
}

/*
 * Scan the filter program and find possible optimization.
 */
static int bpf_jit_optimize(struct bpf_insn *prog, u_int nins) {
  int flags;
  u_int i;

  /* Do we return immediately? */
  if (BPF_CLASS(prog[0].code) == BPF_RET)
    return (BPF_JIT_FRET);

  for (flags = 0, i = 0; i < nins; i++) {
    switch (prog[i].code) {
      case BPF_LD | BPF_W | BPF_ABS:
      case BPF_LD | BPF_H | BPF_ABS:
      case BPF_LD | BPF_B | BPF_ABS:
      case BPF_LD | BPF_W | BPF_IND:
      case BPF_LD | BPF_H | BPF_IND:
      case BPF_LD | BPF_B | BPF_IND:
      case BPF_LDX | BPF_MSH | BPF_B:
        flags |= BPF_JIT_FPKT;
        break;
      case BPF_LD | BPF_MEM:
      case BPF_LDX | BPF_MEM:
      case BPF_ST:
      case BPF_STX:
        flags |= BPF_JIT_FMEM;
        break;
      case BPF_LD | BPF_W | BPF_LEN:
      case BPF_LDX | BPF_W | BPF_LEN:
        flags |= BPF_JIT_FLEN;
        break;
      case BPF_JMP | BPF_JA:
      case BPF_JMP | BPF_JGT | BPF_K:
      case BPF_JMP | BPF_JGE | BPF_K:
      case BPF_JMP | BPF_JEQ | BPF_K:
      case BPF_JMP | BPF_JSET | BPF_K:
      case BPF_JMP | BPF_JGT | BPF_X:
      case BPF_JMP | BPF_JGE | BPF_X:
      case BPF_JMP | BPF_JEQ | BPF_X:
      case BPF_JMP | BPF_JSET | BPF_X:
        flags |= BPF_JIT_FJMP;
        break;
    }
    if (flags == BPF_JIT_FLAG_ALL)
      break;
  }

  return (flags);
}

/*
 * Function that does the real stuff.
 */
static bpf_filter_func_t bpf_jit_compile(struct bpf_insn *prog, u_int nins,
                                         size_t *size) {
  bpf_bin_stream stream;
  struct bpf_insn *ins;
  int flags, fret, fpkt, fmem, fjmp, flen;
  u_int i, pass;

  /*
   * NOTE: Do not modify the name of this variable, as it's used by
   * the macros to emit code.
   */
  emit_func emitm;

  flags = bpf_jit_optimize(prog, nins);
  fret = (flags & BPF_JIT_FRET) != 0;
  fpkt = (flags & BPF_JIT_FPKT) != 0;
  fmem = (flags & BPF_JIT_FMEM) != 0;
  fjmp = (flags & BPF_JIT_FJMP) != 0;
  flen = (flags & BPF_JIT_FLEN) != 0;

  if (fret)
    nins = 1;

  memset(&stream, 0, sizeof(stream));

  /* Allocate the reference table for the jumps. */
  if (fjmp) {
    stream.refs = static_cast<uint *>(calloc(nins + 1, sizeof(u_int)));
    if (stream.refs == nullptr)
      return (nullptr);
  }

  /*
   * The first pass will emit the lengths of the instructions
   * to create the reference table.
   */
  emitm = emit_length;

  for (pass = 0; pass < 2; pass++) {
    ins = prog;

    /* Create the procedure header. */
    if (fmem) {
      PUSH(RBP);
      MOVrq(RSP, RBP);
      SUBib(BPF_MEMWORDS * sizeof(uint32_t), RSP);
    }
    if (flen)
      MOVrd2(ESI, R9D);
    if (fpkt) {
      MOVrq2(RDI, R8);
      MOVrd(EDX, EDI);
    }

    for (i = 0; i < nins; i++) {
      stream.bpf_pc++;

      switch (ins->code) {
        default:
          abort();

        case BPF_RET | BPF_K:
          MOVid(ins->k, EAX);
          if (fmem)
            LEAVE();
          RET();
          break;

        case BPF_RET | BPF_A:
          if (fmem)
            LEAVE();
          RET();
          break;

        case BPF_LD | BPF_W | BPF_ABS:
          MOVid(ins->k, ESI);
          CMPrd(EDI, ESI);
          JAb(12);
          MOVrd(EDI, ECX);
          SUBrd(ESI, ECX);
          CMPid(sizeof(int32_t), ECX);
          if (fmem) {
            JAEb(4);
            ZEROrd(EAX);
            LEAVE();
          } else {
            JAEb(3);
            ZEROrd(EAX);
          }
          RET();
          MOVrq3(R8, RCX);
          MOVobd(RCX, RSI, EAX);
          BSWAP(EAX);
          break;

        case BPF_LD | BPF_H | BPF_ABS:
          ZEROrd(EAX);
          MOVid(ins->k, ESI);
          CMPrd(EDI, ESI);
          JAb(12);
          MOVrd(EDI, ECX);
          SUBrd(ESI, ECX);
          CMPid(sizeof(int16_t), ECX);
          if (fmem) {
            JAEb(2);
            LEAVE();
          } else
            JAEb(1);
          RET();
          MOVrq3(R8, RCX);
          MOVobw(RCX, RSI, AX);
          SWAP_AX();
          break;

        case BPF_LD | BPF_B | BPF_ABS:
          ZEROrd(EAX);
          MOVid(ins->k, ESI);
          CMPrd(EDI, ESI);
          if (fmem) {
            JBb(2);
            LEAVE();
          } else
            JBb(1);
          RET();
          MOVrq3(R8, RCX);
          MOVobb(RCX, RSI, AL);
          break;

        case BPF_LD | BPF_W | BPF_LEN:
          MOVrd3(R9D, EAX);
          break;

        case BPF_LDX | BPF_W | BPF_LEN:
          MOVrd3(R9D, EDX);
          break;

        case BPF_LD | BPF_W | BPF_IND:
          CMPrd(EDI, EDX);
          JAb(27);
          MOVid(ins->k, ESI);
          MOVrd(EDI, ECX);
          SUBrd(EDX, ECX);
          CMPrd(ESI, ECX);
          JBb(14);
          ADDrd(EDX, ESI);
          MOVrd(EDI, ECX);
          SUBrd(ESI, ECX);
          CMPid(sizeof(int32_t), ECX);
          if (fmem) {
            JAEb(4);
            ZEROrd(EAX);
            LEAVE();
          } else {
            JAEb(3);
            ZEROrd(EAX);
          }
          RET();
          MOVrq3(R8, RCX);
          MOVobd(RCX, RSI, EAX);
          BSWAP(EAX);
          break;

        case BPF_LD | BPF_H | BPF_IND:
          ZEROrd(EAX);
          CMPrd(EDI, EDX);
          JAb(27);
          MOVid(ins->k, ESI);
          MOVrd(EDI, ECX);
          SUBrd(EDX, ECX);
          CMPrd(ESI, ECX);
          JBb(14);
          ADDrd(EDX, ESI);
          MOVrd(EDI, ECX);
          SUBrd(ESI, ECX);
          CMPid(sizeof(int16_t), ECX);
          if (fmem) {
            JAEb(2);
            LEAVE();
          } else
            JAEb(1);
          RET();
          MOVrq3(R8, RCX);
          MOVobw(RCX, RSI, AX);
          SWAP_AX();
          break;

        case BPF_LD | BPF_B | BPF_IND:
          ZEROrd(EAX);
          CMPrd(EDI, EDX);
          JAEb(13);
          MOVid(ins->k, ESI);
          MOVrd(EDI, ECX);
          SUBrd(EDX, ECX);
          CMPrd(ESI, ECX);
          if (fmem) {
            JAb(2);
            LEAVE();
          } else
            JAb(1);
          RET();
          MOVrq3(R8, RCX);
          ADDrd(EDX, ESI);
          MOVobb(RCX, RSI, AL);
          break;

        case BPF_LDX | BPF_MSH | BPF_B:
          MOVid(ins->k, ESI);
          CMPrd(EDI, ESI);
          if (fmem) {
            JBb(4);
            ZEROrd(EAX);
            LEAVE();
          } else {
            JBb(3);
            ZEROrd(EAX);
          }
          RET();
          ZEROrd(EDX);
          MOVrq3(R8, RCX);
          MOVobb(RCX, RSI, DL);
          ANDib(0x0f, DL);
          SHLib(2, EDX);
          break;

        case BPF_LD | BPF_IMM:
          MOVid(ins->k, EAX);
          break;

        case BPF_LDX | BPF_IMM:
          MOVid(ins->k, EDX);
          break;

        case BPF_LD | BPF_MEM:
          MOVid(ins->k * sizeof(uint32_t), ESI);
          MOVobd(RSP, RSI, EAX);
          break;

        case BPF_LDX | BPF_MEM:
          MOVid(ins->k * sizeof(uint32_t), ESI);
          MOVobd(RSP, RSI, EDX);
          break;

        case BPF_ST:
          /*
           * XXX this command and the following could
           * be optimized if the previous instruction
           * was already of this type
           */
          MOVid(ins->k * sizeof(uint32_t), ESI);
          MOVomd(EAX, RSP, RSI);
          break;

        case BPF_STX:
          MOVid(ins->k * sizeof(uint32_t), ESI);
          MOVomd(EDX, RSP, RSI);
          break;

        case BPF_JMP | BPF_JA:
          JUMP(ins->k);
          break;

        case BPF_JMP | BPF_JGT | BPF_K:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          CMPid(ins->k, EAX);
          JCC(JA, JBE);
          break;

        case BPF_JMP | BPF_JGE | BPF_K:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          CMPid(ins->k, EAX);
          JCC(JAE, JB);
          break;

        case BPF_JMP | BPF_JEQ | BPF_K:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          CMPid(ins->k, EAX);
          JCC(JE, JNE);
          break;

        case BPF_JMP | BPF_JSET | BPF_K:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          TESTid(ins->k, EAX);
          JCC(JNE, JE);
          break;

        case BPF_JMP | BPF_JGT | BPF_X:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          CMPrd(EDX, EAX);
          JCC(JA, JBE);
          break;

        case BPF_JMP | BPF_JGE | BPF_X:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          CMPrd(EDX, EAX);
          JCC(JAE, JB);
          break;

        case BPF_JMP | BPF_JEQ | BPF_X:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          CMPrd(EDX, EAX);
          JCC(JE, JNE);
          break;

        case BPF_JMP | BPF_JSET | BPF_X:
          if (ins->jt == ins->jf) {
            JUMP(ins->jt);
            break;
          }
          TESTrd(EDX, EAX);
          JCC(JNE, JE);
          break;

        case BPF_ALU | BPF_ADD | BPF_X:
          ADDrd(EDX, EAX);
          break;

        case BPF_ALU | BPF_SUB | BPF_X:
          SUBrd(EDX, EAX);
          break;

        case BPF_ALU | BPF_MUL | BPF_X:
          MOVrd(EDX, ECX);
          MULrd(EDX);
          MOVrd(ECX, EDX);
          break;

        case BPF_ALU | BPF_DIV | BPF_X:
          TESTrd(EDX, EDX);
          if (fmem) {
            JNEb(4);
            ZEROrd(EAX);
            LEAVE();
          } else {
            JNEb(3);
            ZEROrd(EAX);
          }
          RET();
          MOVrd(EDX, ECX);
          ZEROrd(EDX);
          DIVrd(ECX);
          MOVrd(ECX, EDX);
          break;

        case BPF_ALU | BPF_AND | BPF_X:
          ANDrd(EDX, EAX);
          break;

        case BPF_ALU | BPF_OR | BPF_X:
          ORrd(EDX, EAX);
          break;

        case BPF_ALU | BPF_LSH | BPF_X:
          MOVrd(EDX, ECX);
          SHL_CLrb(EAX);
          break;

        case BPF_ALU | BPF_RSH | BPF_X:
          MOVrd(EDX, ECX);
          SHR_CLrb(EAX);
          break;

        case BPF_ALU | BPF_ADD | BPF_K:
          ADD_EAXi(ins->k);
          break;

        case BPF_ALU | BPF_SUB | BPF_K:
          SUB_EAXi(ins->k);
          break;

        case BPF_ALU | BPF_MUL | BPF_K:
          MOVrd(EDX, ECX);
          MOVid(ins->k, EDX);
          MULrd(EDX);
          MOVrd(ECX, EDX);
          break;

        case BPF_ALU | BPF_DIV | BPF_K:
          MOVrd(EDX, ECX);
          ZEROrd(EDX);
          MOVid(ins->k, ESI);
          DIVrd(ESI);
          MOVrd(ECX, EDX);
          break;

        case BPF_ALU | BPF_AND | BPF_K:
          ANDid(ins->k, EAX);
          break;

        case BPF_ALU | BPF_OR | BPF_K:
          ORid(ins->k, EAX);
          break;

        case BPF_ALU | BPF_LSH | BPF_K:
          SHLib((ins->k) & 0xff, EAX);
          break;

        case BPF_ALU | BPF_RSH | BPF_K:
          SHRib((ins->k) & 0xff, EAX);
          break;

        case BPF_ALU | BPF_NEG:
          NEGd(EAX);
          break;

        case BPF_MISC | BPF_TAX:
          MOVrd(EAX, EDX);
          break;

        case BPF_MISC | BPF_TXA:
          MOVrd(EDX, EAX);
          break;
      }
      ins++;
    }

    if (pass > 0)
      continue;

    *size = stream.cur_ip;
    stream.ibuf =
        static_cast<char *>(mmap(nullptr, *size, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (stream.ibuf == MAP_FAILED) {
      stream.ibuf = nullptr;
      break;
    }

    /*
     * Modify the reference table to contain the offsets and
     * not the lengths of the instructions.
     */
    if (fjmp)
      for (i = 1; i < nins + 1; i++)
        stream.refs[i] += stream.refs[i - 1];

    /* Reset the counters. */
    stream.cur_ip = 0;
    stream.bpf_pc = 0;

    /* The second pass creates the actual code. */
    emitm = emit_code;
  }

  /*
   * The reference table is needed only during compilation,
   * now we can free it.
   */
  if (fjmp)
    free(stream.refs);

  if (stream.ibuf != nullptr &&
      mprotect(stream.ibuf, *size, PROT_READ | PROT_EXEC) != 0) {
    munmap(stream.ibuf, *size);
    stream.ibuf = nullptr;
  }

  return (reinterpret_cast<bpf_filter_func_t>(stream.ibuf));
}

/* -------------------------------------------------------------------------
 * Module code begins from here
 * ------------------------------------------------------------------------- */

/* Note: bpf_filter will return SNAPLEN if matched, and 0 if unmatched. */
/* Note: unmatched packets are sent to gate 0 */
#define SNAPLEN 0xffff

static int compare_filter(const void *filter1, const void *filter2) {
  struct filter *f1 = (struct filter *)filter1;
  struct filter *f2 = (struct filter *)filter2;

  if (f1->priority > f2->priority)
    return -1;
  else if (f1->priority < f2->priority)
    return 1;
  else
    return 0;
}

const Commands BPF::cmds = {
    {"add", "BPFArg", MODULE_CMD_FUNC(&BPF::CommandAdd), 0},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&BPF::CommandClear), 0}};

CommandResponse BPF::Init(const bess::pb::BPFArg &arg) {
  return CommandAdd(arg);
}

void BPF::DeInit() {
  for (int i = 0; i < n_filters_; i++) {
    munmap(reinterpret_cast<void *>(filters_[i].func), filters_[i].mmap_size);
    free(filters_[i].exp);
  }

  n_filters_ = 0;
}

CommandResponse BPF::CommandAdd(const bess::pb::BPFArg &arg) {
  if (n_filters_ + arg.filters_size() > MAX_FILTERS) {
    return CommandFailure(EINVAL, "Too many filters");
  }

  struct filter *filter = &filters_[n_filters_];
  struct bpf_program il_code;

  for (const auto &f : arg.filters()) {
    const char *exp = f.filter().c_str();
    int64_t gate = f.gate();
    if (gate < 0 || gate >= MAX_GATES) {
      return CommandFailure(EINVAL, "Invalid gate");
    }
    if (pcap_compile_nopcap(SNAPLEN, DLT_EN10MB,  // Ethernet
                            &il_code, exp, 1,     // optimize (IL only)
                            PCAP_NETMASK_UNKNOWN) == -1) {
      return CommandFailure(EINVAL, "BPF compilation error");
    }
    filter->priority = f.priority();
    filter->gate = f.gate();
    filter->exp = strdup(exp);
    filter->func =
        bpf_jit_compile(il_code.bf_insns, il_code.bf_len, &filter->mmap_size);
    pcap_freecode(&il_code);
    if (!filter->func) {
      free(filter->exp);
      return CommandFailure(ENOMEM, "BPF JIT compilation error");
    }
    n_filters_++;
    qsort(filters_, n_filters_, sizeof(struct filter), &compare_filter);

    filter++;
  }

  return CommandSuccess();
}

CommandResponse BPF::CommandClear(const bess::pb::EmptyArg &) {
  DeInit();
  return CommandSuccess();
}

inline void BPF::process_batch_1filter(bess::PacketBatch *batch) {
  struct filter *filter = &filters_[0];

  bess::PacketBatch out_batches[2];
  bess::Packet **ptrs[2];

  ptrs[0] = out_batches[0].pkts();
  ptrs[1] = out_batches[1].pkts();

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    int ret;
    int idx;

    ret = filter->func(pkt->head_data<uint8_t *>(), pkt->total_len(),
                       pkt->head_len());

    idx = ret & 1;
    *(ptrs[idx]++) = pkt;
  }

  out_batches[0].set_cnt(ptrs[0] - out_batches[0].pkts());
  out_batches[1].set_cnt(ptrs[1] - out_batches[1].pkts());

  if (out_batches[0].cnt())
    RunChooseModule(0, &out_batches[0]);

  /* matched packets */
  if (out_batches[1].cnt())
    RunChooseModule(filter->gate, &out_batches[1]);
}

void BPF::ProcessBatch(bess::PacketBatch *batch) {
  gate_idx_t out_gates[bess::PacketBatch::kMaxBurst];
  int n_filters = n_filters_;
  int cnt;

  if (n_filters == 0) {
    RunNextModule(batch);
    return;
  }

  if (n_filters == 1) {
    process_batch_1filter(batch);
    return;
  }

  cnt = batch->cnt();

  /* slow version for general cases */
  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    struct filter *filter = &filters_[0];
    gate_idx_t gate = 0; /* default gate for unmatched pkts */

    for (int j = 0; j < n_filters; j++, filter++) {
      if (filter->func(pkt->head_data<uint8_t *>(), pkt->total_len(),
                       pkt->head_len()) != 0) {
        gate = filter->gate;
        break;
      }
    }

    out_gates[i] = gate;
  }

  RunSplit(out_gates, batch);
}

ADD_MODULE(BPF, "bpf", "classifies packets with pcap-filter(7) syntax")
