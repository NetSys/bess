#include <assert.h>
#include <stdio.h>

#include <sys/time.h>

#include <rte_config.h>
#include <rte_common.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include "debug.h"
#include "common.h"
#include "time.h"
#include "task.h"
#include "worker.h"
#include "log.h"
#include "utils/random.h"

#include "tc.h"

/* this library is not thread safe */

static void tc_add_to_parent_pgroup(struct tc *c, int share_resource)
{
	struct tc *parent = c->parent;
	struct pgroup *g = NULL;

	struct cdlist_item *next;

	cdlist_for_each_entry(g, &parent->pgroups, tc) {
		if (c->settings.priority > g->priority) {
			next = &g->tc;
			goto pgroup_init;
		} else if (c->settings.priority == g->priority)
			goto pgroup_add;
	}

	next = (struct cdlist_item *)&parent->pgroups;

pgroup_init:
	g = rte_zmalloc("pgroup", sizeof(*g), 0);
	if (!g)
		oom_crash();

	cdlist_add_before(next, &g->tc);

	heap_init(&g->pq);

	g->resource = share_resource;
	g->priority = c->settings.priority;

	/* fall through */

pgroup_add:
	/* all classes in the pgroup have the same share_resource */
	assert(g->resource == share_resource);

	g->num_children++;
	c->ss.my_pgroup = g;
}

/* TODO: separate tc creation and association with scheduler */
struct tc *tc_init(struct sched *s, const struct tc_params *params)
{
	struct tc *c;

	int ret;
	int i;

	assert(!s->current);

	assert(0 <= params->share_resource);
	assert(params->share_resource < NUM_RESOURCES);

	assert(params->share > 0);
	assert(params->share <= MAX_SHARE);

	c = rte_zmalloc("tc", sizeof(*c), 0);
	if (!c)
		oom_crash();

	ret = ns_insert(NS_TYPE_TC, params->name, c);
	if (ret < 0) {
		rte_free(c);
		return err_to_ptr(ret);
	}

	c->settings = *params;

	tc_inc_refcnt(c);	/* held by user (the owner) */

	c->s = s;
	s->num_classes++;

	c->parent = params->parent ? : &s->root;
	tc_inc_refcnt(c->parent);

	c->last_tsc = rdtsc();

	for (i = 0; i < NUM_RESOURCES; i++) {
		assert(params->limit[i] < (1UL << MAX_LIMIT_POW));
		
		c->tb[i].limit = (params->limit[i] << (USAGE_AMPLIFIER_POW - 4)) 
				/ (tsc_hz >> 4);

		if (c->tb[i].limit) {
			assert(params->max_burst[i] < (1UL << MAX_LIMIT_POW));
			c->tb[i].max_burst = (params->max_burst[i] << 
					(USAGE_AMPLIFIER_POW - 4)) / (tsc_hz >> 4);
			c->has_limit = 1;
		}

		c->tb[i].tokens = 0;
	}
	
	c->ss.stride = STRIDE1 / params->share;
	c->ss.pass = 0;			/* will be set when joined */

	cdlist_head_init(&c->tasks);
	cdlist_head_init(&c->pgroups);

	tc_add_to_parent_pgroup(c, params->share_resource);

	cdlist_add_tail(&s->tcs_all, &c->sched_all);

	return c;
}

void _tc_do_free(struct tc *c)
{
	struct pgroup *g = c->ss.my_pgroup;
	struct tc *parent = c->parent;
	
	assert(c->refcnt == 0);

	assert(!c->state.queued);
	assert(!c->state.throttled);

	assert(cdlist_is_empty(&c->pgroups));
	assert(cdlist_is_empty(&c->tasks));

	if (g) {
		g->num_children--;
		if (g->num_children == 0) {
			cdlist_del(&g->tc);
			heap_close(&g->pq);
			rte_free(g);
		}

		cdlist_del(&c->sched_all);
		c->s->num_classes--;
	}

	ns_remove(c->settings.name);

	memset(c, 0, sizeof(*c));	/* zero out to detect potential bugs */
	rte_free(c);			/* Note: c is struct sched, if root */
	
	if (parent)
		tc_dec_refcnt(parent);
}

static inline int tc_is_root(struct tc *c)
{
	return c->parent == NULL;
}

void tc_join(struct tc *c)
{
	struct pgroup *g = c->ss.my_pgroup;
	struct heap *pq = &g->pq;
	struct tc *next = heap_peek(pq);

	assert(!c->state.queued);
	assert(!c->state.runnable);

	c->state.runnable = 1;

	if (!c->state.throttled) {
		c->state.queued = 1;
		c->ss.pass = (next ? next->ss.pass : 0) + c->ss.remain;
		heap_push(pq, c->ss.pass, c);
		tc_inc_refcnt(c);
	}
}

void tc_leave(struct tc *c)
{
	struct pgroup *g = c->ss.my_pgroup;
	struct heap *pq = &g->pq;
	struct tc *next = heap_peek(pq);

	/* if not joined yet, do nothing */
	if (!c->state.runnable)
		return;

	c->state.runnable = 0;
	c->ss.remain = c->ss.pass - next->ss.pass;
}

struct sched *sched_init()
{
	struct sched *s;

	s = rte_zmalloc("sched", sizeof(*s), 0);
	if (!s)
		oom_crash();

	s->root.refcnt = 1;
	cdlist_head_init(&s->root.tasks);	/* this will be always empty */
	cdlist_head_init(&s->root.pgroups);

	heap_init(&s->pq);

	cdlist_head_init(&s->tcs_all);

	return s;
}

/* Deallocate the scheduler. The owner is still responsible to release
 * its references to all traffic classes */
void sched_free(struct sched *s)
{
	struct tc *c;
	struct tc *next;

	cdlist_for_each_entry_safe(c, next, &s->tcs_all, sched_all) {
		if (c->state.queued) {
			c->state.queued = 0;
			tc_dec_refcnt(c);
		}

		if (c->state.throttled) {
			c->state.throttled = 0;
			tc_dec_refcnt(c);
		}
	}

	heap_close(&s->pq);

	/* the actual memory block of s will be freed by the root TC
	 * since it shares the address with this scheduler */
	tc_dec_refcnt(&s->root);
}

static void resume_throttled(struct sched *s, uint64_t tsc)
{
	while (s->pq.num_nodes > 0) {
		struct tc *c;
		int64_t event_tsc;

		heap_peek_valdata(&s->pq, &event_tsc, (void **)&c);

		if (event_tsc > tsc)
			break;

		heap_pop(&s->pq);

		c->state.throttled = 0;
		
		if (c->state.runnable) {
			/* No refcnt is adjusted, since we transfer 
			 * s->pq's reference to my_pgroup->pq */ 
			c->state.queued = 1;
			c->last_tsc = event_tsc;
			heap_push(&c->ss.my_pgroup->pq, 0, c);
		} else
			tc_dec_refcnt(c);
	}
}

/* FIXME: this non-recursive version is buggy. Use a stack */
static struct tc *pick(struct tc *c)
{
	struct pgroup *g;

again:
	/* found a leaf? */
	if (cdlist_is_empty(&c->pgroups))
		return c;

	cdlist_for_each_entry(g, &c->pgroups, tc) {
		struct heap *pq = &g->pq;
		struct tc *child;

		child = heap_peek(pq);
		if (!child)
			continue;

		assert(child->state.queued);

		if (!child->state.runnable)
			return child;
	
		c = child;
		goto again;
	}

	return NULL;
}

static struct tc *sched_next(struct sched *s, uint64_t tsc)
{
	struct tc *c;

	assert(!s->current);
	
	resume_throttled(s, tsc);

again:
	c = pick(&s->root);

	/* empty tree? */
	if (c == &s->root)
		c = NULL;

	if (c) {
		if (!c->state.runnable) {
			c->state.queued = 0;
			heap_pop(&c->ss.my_pgroup->pq);
			tc_dec_refcnt(c);

			goto again;
		}

		s->current = c;
	}

	return c;
}

/* TODO: the vector version is hella slow. fix it. */
/* acc += x */
static inline void accumulate(resource_arr_t acc, resource_arr_t x)
{
	uint64_t * restrict p1 = acc;
	uint64_t * restrict p2 = x;

#if 0 && __AVX2__
	*((__m256i *)p1) = _mm256_add_epi64(*((__m256i *)p1), *((__m256i *)p2));
#elif 0 && __AVX__
	*((__m128i *)p1+0) = _mm_add_epi64(*((__m128i *)p1+0), *((__m128i *)p2+0));
	*((__m128i *)p1+1) = _mm_add_epi64(*((__m128i *)p1+1), *((__m128i *)p2+1));
#else
	for (int i = 0; i < NUM_RESOURCES; i++)
		p1[i] += p2[i];
#endif
}

/* returns 1 if it has been throttled */
static int tc_account(struct sched *s, struct tc *c, 
		resource_arr_t usage, uint64_t tsc)
{
	uint64_t elapsed_cycles;
	uint64_t max_wait_tsc;

	int throttled;

	int i;

	accumulate(c->stats.usage, usage);

	if (!c->has_limit) {
		c->last_tsc = tsc;
		return 0;
	}

	elapsed_cycles = tsc - c->last_tsc;
	c->last_tsc = tsc;

	max_wait_tsc = 0;
	throttled = 0;

	for (i = 0; i < NUM_RESOURCES; i++) {
		uint64_t consumed;
		uint64_t tokens;

		const uint64_t limit = c->tb[i].limit;

		if (!limit)
			continue;

		consumed = usage[i] << USAGE_AMPLIFIER_POW;
		tokens = c->tb[i].tokens + limit * elapsed_cycles;

		if (tokens < consumed) {
			uint64_t wait_tsc;

			wait_tsc = (consumed - tokens) / limit;

			throttled = 1;

			if (wait_tsc > max_wait_tsc)
				max_wait_tsc = wait_tsc;
		} else
			c->tb[i].tokens = RTE_MIN(tokens - consumed, 
					c->tb[i].max_burst);
	}

	if (throttled) {
		for (i = 0; i < NUM_RESOURCES; i++)
			c->tb[i].tokens = 0;

		c->state.throttled = 1;
		c->stats.cnt_throttled++;

		heap_push(&s->pq, tsc + max_wait_tsc, c);
		tc_inc_refcnt(c);

		return 1;
	}
		
	return 0;
}

/* must be called after the previous sched_next() */
static void sched_done(struct sched *s, struct tc *c, 
		resource_arr_t usage, int reschedule, uint64_t tsc)
{
	accumulate(s->stats.usage, usage);

	assert(s->current);
	s->current = NULL;

	if (!reschedule)
		c->state.runnable = 0;

	/* upwards from the leaf, skipping the root class */
	do { 
		struct pgroup *g = c->ss.my_pgroup;
		struct heap *pq = &g->pq;

		uint64_t consumed = usage[g->resource];

		int throttled;

		assert(c->state.queued);
		c->ss.pass += c->ss.stride * consumed / QUANTUM;

		throttled = tc_account(s, c, usage, tsc);
		if (throttled) 
			reschedule = 0;

		if (reschedule) {
			heap_replace(pq, c->ss.pass, c);
		} else {
			struct tc *next;

			c->state.queued = 0;
			heap_pop(pq);
			tc_dec_refcnt(c);

			next = heap_peek(pq);
			c->ss.remain = c->ss.pass - (next ? next->ss.pass : 0);

			reschedule = (pq->num_nodes > 0);
		}

		c = c->parent;
	} while (!tc_is_root(c));
}

/* print out all resource usage fields */
static char *print_tc_stats_detail(struct sched *s, char *p, int max_cnt)
{
	const char *fields[] = {
		"count",
		"cycles",
		"packets",
		"bits",
		"throttled",
	};

	const int num_fields = sizeof(fields) / sizeof(sizeof(const char *));

	struct tc *c;

	int num_printed;

	ct_assert(sizeof(struct tc_stats) >= sizeof(fields));

	p += sprintf(p, "\n");

	if (cdlist_is_empty(&s->tcs_all))
		return p;

	p += sprintf(p, "%-10s ", "TC");
	num_printed = 0;

	cdlist_for_each_entry(c, &s->tcs_all, sched_all) {
		if (num_printed < max_cnt) {
			p += sprintf(p, "%12s", c->settings.name);
		} else {
			p += sprintf(p, " ...");
			break;
		}
		num_printed++;
	}
	p += sprintf(p, "\n");

	for (int i = 0; i < num_fields; i++) {
		p += sprintf(p, "%-10s ", fields[i]);
		num_printed = 0;

		cdlist_for_each_entry(c, &s->tcs_all, sched_all) {
			uint64_t value;

#define COUNTER 	(((uint64_t *)&c->stats)[i])
#define LAST_COUNTER 	(((uint64_t *)&c->last_stats)[i])

			value = COUNTER - LAST_COUNTER;
			LAST_COUNTER = COUNTER;

#undef COUNTER
#undef LAST_COUNTER

			if (num_printed < max_cnt) {
				p += sprintf(p, "%12lu", value);
			} else {
				p += sprintf(p, " ...");
				break;
			}
			num_printed++;
		}
		p += sprintf(p, "\n");
	}

	p += sprintf(p, "\n");

	return p;
}

static char *print_tc_stats_simple(struct sched *s, char *p, int max_cnt)
{
	struct tc *c;

	int num_printed = 0;

	cdlist_for_each_entry(c, &s->tcs_all, sched_all) {
		uint64_t cnt;
		uint64_t cycles;
		uint64_t pkts;
		uint64_t bits;

		cnt = c->stats.usage[RESOURCE_CNT] - 
				c->last_stats.usage[RESOURCE_CNT];

		cycles = c->stats.usage[RESOURCE_CYCLE] - 
				c->last_stats.usage[RESOURCE_CYCLE];

		pkts = c->stats.usage[RESOURCE_PACKET] - 
				c->last_stats.usage[RESOURCE_PACKET];

		bits = c->stats.usage[RESOURCE_BIT] - 
			c->last_stats.usage[RESOURCE_BIT];

		c->last_stats = c->stats;

		p += sprintf(p, "\tC%s %.1f%%(%.2fM) %.3fMpps %.1fMbps", 
				c->settings.name, 
				cycles * 100.0 / tsc_hz, 
				cnt / 1000000.0,
				pkts / 1000000.0, 
				bits / 1000000.0);

		num_printed++;

		if (num_printed >= max_cnt) {
			p += sprintf(p, "\t... (%d more)", 
					s->num_classes - max_cnt);
			break;
		}
	}

	p += sprintf(p, "\n");

	return p;
}

static void print_stats(struct sched *s, struct sched_stats *last_stats)
{
	uint64_t cycles_idle;
	uint64_t cnt_idle;
	uint64_t cnt;
	uint64_t cycles;
	uint64_t pkts;
	uint64_t bits;

	char buf[65536];
	char *p;

	cycles_idle = s->stats.cycles_idle - last_stats->cycles_idle;
	cnt_idle = s->stats.cnt_idle - last_stats->cnt_idle;

	cnt = s->stats.usage[RESOURCE_CNT] - 
			last_stats->usage[RESOURCE_CNT];

	cycles = s->stats.usage[RESOURCE_CYCLE] - 
		last_stats->usage[RESOURCE_CYCLE];

	pkts = s->stats.usage[RESOURCE_PACKET] - 
			last_stats->usage[RESOURCE_PACKET];

	bits = s->stats.usage[RESOURCE_BIT] - 
		last_stats->usage[RESOURCE_BIT];

	p = buf;
	p += sprintf(p, "W%d: idle %.1f%%(%.1fM) "
			"total %.1f%%(%.1fM) %.3fMpps %.1fMbps ", 
			ctx.wid, 
			cycles_idle * 100.0 / tsc_hz,
			cnt_idle / 1000000.0,
			cycles * 100.0 / tsc_hz,
			cnt / 1000000.0,
			pkts / 1000000.0,
			bits / 1000000.0);

#if 0
	p = print_tc_stats_simple(s, p, 5);
#else
	p = print_tc_stats_detail(s, p, 16);
#endif

	log_info("%s", buf);
}

static inline struct task_result tc_scheduled(struct tc *c)
{
	struct task_result ret;
	struct task *t;

	int num_tasks = c->num_tasks;

	while (num_tasks--) {
		t = container_of(cdlist_rotate_left(&c->tasks), struct task, tc);

		ret = task_scheduled(t);
		if (ret.packets)
			return ret;
	}

	return (struct task_result){.packets = 0, .bits = 0};
}

void sched_loop(struct sched *s)
{
	struct sched_stats last_stats = s->stats;
	uint64_t last_print_tsc;
	uint64_t checkpoint;
	uint64_t now;

	last_print_tsc = checkpoint = now = rdtsc();

	/* the main scheduling - running - accounting loop */
	for (uint64_t round = 0; ; round++) {
		struct tc *c;
		struct task_result ret;
		resource_arr_t usage;

		/* periodic check for every 2^8 rounds,
		 * to mitigate expensive operations */
		if ((round & 0xff) == 0) {
			if (unlikely(is_pause_requested())) {
				if (unlikely(block_worker()))
					break;
				last_stats = s->stats;
				last_print_tsc = checkpoint = now = rdtsc();
			} else if (unlikely(global_opts.print_tc_stats &&
					now - last_print_tsc >= tsc_hz)) {
				print_stats(s, &last_stats);
				last_stats = s->stats;
				last_print_tsc = checkpoint = now = rdtsc();
			}
		}

		/* Schedule (S) */
		c = sched_next(s, now);

		if (c) {
			/* Running (R) */
			ctx.current_tsc = now;	/* tasks see updated tsc */
			ret = tc_scheduled(c);

			now = rdtsc();

			/* Accounting (A) */
			usage[RESOURCE_CNT] = 1;
			usage[RESOURCE_CYCLE] = now - checkpoint;
			usage[RESOURCE_PACKET] = ret.packets;
			usage[RESOURCE_BIT] = ret.bits;

			sched_done(s, c, usage, 1, now);
		} else {
			now = rdtsc();

			s->stats.cnt_idle++;
			s->stats.cycles_idle += (now - checkpoint);
		}
	
		checkpoint = now;
	}
}

static uint64_t get_usec(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}

void sched_test_alloc()
{
	const int num_classes = 100000;

	struct sched *s;
	struct tc *classes[num_classes];

	uint64_t seed = get_usec();

	int i;

	s = sched_init();

	/* generate a random tree */
	for (i = 0; i < num_classes; i++) {
		struct tc_params params;
		int parent_id = rand_fast(&seed) % (i + 1);

		params = (struct tc_params) {
			.parent = parent_id ? classes[(parent_id - 1)] : NULL,
			.priority = rand_fast(&seed) % 8,
			.share = 1,
		};

		/* params.share_resource = rand_fast(&seed) % 2, should fail */
		params.share_resource = params.priority % NUM_RESOURCES,
		
		classes[i] = tc_init(s, &params);
	}

	assert(s->num_classes == num_classes);

#if 1
	/* shuffle */
	for (i = num_classes - 1; i > 0; i--) {
		struct tc *tmp;
		int j;
		
		j = rand_fast(&seed) % (i + 1);
		tmp = classes[j];
		classes[j] = classes[i];
		classes[i] = tmp;
	}

	for (i = 0; i < num_classes; i++)
		tc_dec_refcnt(classes[i]);

	assert(s->root.refcnt == 1);
	assert(s->num_classes == 0);
	assert(cdlist_is_empty(&s->root.pgroups));
#endif

	sched_free(s);

	log_debug("SCHED: test passed\n");
}

void sched_test_perf()
{
#if 1
	const int num_classes = 50;	/* CPU bound */
#else
	const int num_classes = 1000;	/* cache bound */
#endif

	struct sched *s;
	struct tc *classes[num_classes];

	int i;

	s = sched_init();

	for (i = 0; i < num_classes; i++) {
		struct tc_params params = {
			.parent = NULL,
			.priority = 0,
			.share = 1,
			.share_resource = RESOURCE_BIT,
		};

		if (i % 3 == 0)
			params.limit[RESOURCE_PACKET] = 1e5;

		if (i % 2 == 0)
			params.limit[RESOURCE_BIT] = 100e6;
		
		classes[i] = tc_init(s, &params);
	}

	for (i = 0; i < num_classes; i++)
		tc_join(classes[i]);

	sched_loop(s);
}
