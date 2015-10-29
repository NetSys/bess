#ifndef _DEBUG_H_
#define _DEBUG_H_

/* #define RTE_LIBRTE_MBUF_DEBUG */

#define ct_assert(p)	_Static_assert(p, "Compile-time assertion failure")

void dump_types(void);

#endif
