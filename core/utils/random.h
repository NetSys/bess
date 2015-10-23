#ifndef __RANDOM_H__
#define __RANDOM_H__

static inline uint32_t rand_fast(uint64_t* seed)
{
	*seed = *seed * 1103515245 + 12345;
	return (*seed) >> 32;
}

#endif
