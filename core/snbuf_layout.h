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

#ifndef BESS_SNBUFLAYOUT_H_
#define BESS_SNBUFLAYOUT_H_

/* snbuf and mbuf share the same start address, so that we can avoid conversion.
 *
 * Layout (2560 bytes):
 *    Offset	Size	Field
 *  - 0		128	mbuf (SNBUF_MBUF == sizeof(struct rte_mbuf))
 *  - 128	64	some read-only/immutable fields
 *  - 192	128	static/dynamic metadata fields
 *  - 320	64	private area for module/driver's internal use
 *                        (currently used for vport RX/TX descriptors)
 *  - 384	128	_headroom (SNBUF_HEADROOM == RTE_PKTMBUF_HEADROOM)
 *  - 512	2048	_data (SNBUF_DATA)
 *
 * Stride will be 2624B, because of mempool's per-object header which takes 64B.
 *
 * Invariants:
 *  * When packets are newly allocated, the data should be filled from _data.
 *  * The packet data may reside in the _headroom + _data areas,
 *    but its size must not exceed 2048 (SNBUF_DATA) when passed to a port.
 */
#define SNBUF_MBUF 128
#define SNBUF_IMMUTABLE 64
#define SNBUF_METADATA 128
#define SNBUF_SCRATCHPAD 64
#define SNBUF_RESERVE (SNBUF_IMMUTABLE + SNBUF_METADATA + SNBUF_SCRATCHPAD)
#define SNBUF_HEADROOM 128
#define SNBUF_DATA 2048

#define SNBUF_MBUF_OFF 0
#define SNBUF_IMMUTABLE_OFF SNBUF_MBUF
#define SNBUF_METADATA_OFF (SNBUF_IMMUTABLE_OFF + SNBUF_IMMUTABLE)
#define SNBUF_SCRATCHPAD_OFF (SNBUF_METADATA_OFF + SNBUF_METADATA)
#define SNBUF_HEADROOM_OFF (SNBUF_SCRATCHPAD_OFF + SNBUF_SCRATCHPAD)
#define SNBUF_DATA_OFF (SNBUF_HEADROOM_OFF + SNBUF_HEADROOM)

#define SNBUF_SIZE (SNBUF_DATA_OFF + SNBUF_DATA)

#endif  // BESS_SNBUFLAYOUT_H_
