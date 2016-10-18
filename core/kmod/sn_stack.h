#ifndef __SN_STACK_H__
#define __SN_STACK_H__

#define SN_STACK_MAX_LEN 1024
typedef struct sn_stack {
	void *items[SN_STACK_MAX_LEN];
	int len;
} sn_stack_t;

__attribute__((unused)) static void sn_stack_init(sn_stack_t *stack)
{
	stack->len = 0;
}

__attribute__((unused)) static inline int sn_stack_push(sn_stack_t *stack,
							void **objs, int len)
{
	int i;
	if (stack->len + len > SN_STACK_MAX_LEN)
		return -1;

	for (i = 0; i < len; i++) {
		stack->items[i + stack->len] = objs[i];
	}
	stack->len += len;

	return 0;
}

__attribute__((unused)) static inline int sn_stack_pop(sn_stack_t *stack,
						       void **objs, int len)
{
	int i;
	if (len > stack->len)
		return -1;

	for (i = 0; i < len; i++) {
		stack->len--;
		objs[i] = stack->items[stack->len];
	}
	return 0;
}

#endif
