#ifndef __RANDOM_H__
#define __RANDOM_H__

#include <stdint.h>

static inline uint32_t rand_fast(uint64_t *seed)
{
	uint64_t next_seed;
	next_seed = *seed * 1103515245 + 12345;
	*seed = next_seed;
	return next_seed >> 32;
}

/* returns [0, range) with no integer modulo operation */
static inline uint32_t rand_fast_range(uint64_t *seed, uint32_t range)
{
	uint64_t next_seed;

	union {
		uint64_t i;
		double d;
	} tmp;

	next_seed = *seed * 1103515245 + 12345;

	/*
	 * From the MSB,
	 * 0: sign
	 * 1-11: exponent (0x3ff == 0, 0x400 == 1)
	 * 12-63: mantissa
	 * The resulting double number is 1.(b0)(b1)...(b47),
	 * where next_seed is (b0)(b1)...(b63).
	 */
	tmp.i = (next_seed >> 12) | 0x3ff0000000000000ul;

	*seed = next_seed;

	return (tmp.d - 1.0) * range;
}

#endif
