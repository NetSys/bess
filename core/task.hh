#ifndef _TASK_H_
#define _TASK_H_

#include <stdint.h>

#include "utils/cdlist.h"

#include "tc.h"

extern struct cdlist_head all_tasks;

typedef uint16_t task_id_t;

#define INVALID_TASK_ID		((task_id_t)-1)
ct_assert(MAX_TASKS_PER_MODULE < INVALID_TASK_ID);

struct task_result {
	uint64_t packets;
	uint64_t bits;
};


#endif
