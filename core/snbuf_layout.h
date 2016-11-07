#ifndef BESS_SNBUFLAYOUT_H_
#define BESS_SNBUFLAYOUT_H_

/* snbuf and mbuf share the same start address, so that we can avoid conversion.
 *
 * Layout (2048 bytes):
 *    Offset	Size	Field
 *  - 0		128	mbuf (SNBUF_MBUF == sizeof(struct rte_mbuf))
 *  - 128	64	some read-only/immutable fields
 *  - 192	128	static/dynamic metadata fields
 *  - 320	64	private area for module/driver's internal use
 *                        (currently used for vport RX/TX descriptors)
 *  - 384	128	_headroom (SNBUF_HEADROOM == RTE_PKTMBUF_HEADROOM)
 *  - 512	1536	_data (SNBUF_DATA)
 *
 * Stride will be 2112B, because of mempool's per-object header which takes 64B.
 *
 * Invariants:
 *  * When packets are newly allocated, the data should be filled from _data.
 *  * The packet data may reside in the _headroom + _data areas,
 *    but its size must not exceed 1536 (SNBUF_DATA) when passed to a port.
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

#endif  // BESS_SNBUFLAYOUT_H_
