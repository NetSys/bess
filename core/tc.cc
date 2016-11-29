#include "tc.h"

#include <cassert>
#include <cinttypes>
#include <cstdio>

#include <glog/logging.h>

#include "debug.h"
#include "mem_alloc.h"
#include "opts.h"
#include "task.h"
#include "utils/common.h"
#include "utils/random.h"
#include "utils/time.h"
#include "worker.h"

// TODO(barath): move this global container of TCs to the TC class once it
// exists.
namespace TCContainer {
std::unordered_map<std::string, struct tc *> tcs;
}  // TCContainer

/* this library is not thread safe */

static void tc_add_to_parent_pgroup(struct tc *c, int share_resource) {
  struct tc *parent = c->parent;
  struct pgroup *g = nullptr;

  struct cdlist_item *next;

  cdlist_for_each_entry(g, &parent->pgroups, tc) {
    if (c->settings.priority > g->priority) {
      next = &g->tc;
      goto pgroup_init;
    } else if (c->settings.priority == g->priority) {
      goto pgroup_add;
    }
  }

  next = (struct cdlist_item *)&parent->pgroups;

pgroup_init:
  g = (struct pgroup *)mem_alloc(sizeof(*g));
  if (!g) {
    abort();
  }

  cdlist_add_before(next, &g->tc);

  heap_init(&g->pq);

  g->resource = share_resource;
  g->priority = c->settings.priority;

/* fall through */

pgroup_add:
  /* all classes in the pgroup have the same share_resource */
  DCHECK_EQ(g->resource, share_resource);

  g->num_children++;
  c->ss.my_pgroup = g;
}

/* TODO: separate tc creation and association with scheduler */
struct tc *tc_init(struct sched *s, const struct tc_params *params,
                   struct tc *parent) {
  struct tc *c;

  int i;

  DCHECK(!s->current);

  DCHECK_LE(0, params->share_resource);
  DCHECK_LT(params->share_resource, NUM_RESOURCES);

  DCHECK_GT(params->share, 0);
  DCHECK_LE(params->share, MAX_SHARE);

  c = (struct tc *)mem_alloc(sizeof(*c));
  if (!c) {
    abort();
  }

  if (!TCContainer::tcs.insert({params->name, c}).second) {
    LOG(ERROR) << "Can't insert TC named " << params->name
               << "TCContainer::tcs.size()=" << TCContainer::tcs.size();
    mem_free(c);
    return (struct tc *)err_to_ptr(-EEXIST);
  }

  c->settings = *params;

  tc_inc_refcnt(c); /* held by user (the owner) */

  c->s = s;
  s->num_classes++;

  c->parent = parent ?: &s->root;
  tc_inc_refcnt(c->parent);

  c->last_tsc = rdtsc();

  for (i = 0; i < NUM_RESOURCES; i++) {
    DCHECK_LT(params->limit[i], ((uint64_t)1 << MAX_LIMIT_POW));

    c->tb[i].limit =
        (params->limit[i] << (USAGE_AMPLIFIER_POW - 4)) / (tsc_hz >> 4);

    if (c->tb[i].limit) {
      DCHECK_LT(params->max_burst[i], ((uint64_t)1 << MAX_LIMIT_POW));
      c->tb[i].max_burst =
          (params->max_burst[i] << (USAGE_AMPLIFIER_POW - 4)) / (tsc_hz >> 4);
      c->has_limit = 1;
    }

    c->tb[i].tokens = 0;
  }

  c->ss.stride = STRIDE1 / params->share;
  c->ss.pass = 0; /* will be set when joined */

  cdlist_head_init(&c->tasks);
  cdlist_head_init(&c->pgroups);

  tc_add_to_parent_pgroup(c, params->share_resource);

  cdlist_add_tail(&s->tcs_all, &c->sched_all);

  return c;
}

void _tc_do_free(struct tc *c) {
  struct pgroup *g = c->ss.my_pgroup;
  struct tc *parent = c->parent;

  DCHECK_EQ(c->refcnt, 0);

  DCHECK(!c->state.queued);
  DCHECK(!c->state.throttled);

  DCHECK(cdlist_is_empty(&c->pgroups));
  DCHECK(cdlist_is_empty(&c->tasks));

  if (g) {
    g->num_children--;
    if (g->num_children == 0) {
      cdlist_del(&g->tc);
      heap_close(&g->pq);
      mem_free(g);
    }

    cdlist_del(&c->sched_all);
    c->s->num_classes--;
  }

  if (parent) {
    DCHECK(TCContainer::tcs.erase(c->settings.name));
  }

  memset(c, 0, sizeof(*c)); /* zero out to detect potential bugs */
  mem_free(c);              /* Note: c is struct sched, if root */

  if (parent) {
    tc_dec_refcnt(parent);
  }
}

static inline int tc_is_root(struct tc *c) {
  return c->parent == nullptr;
}

static inline int64_t next_pass(struct heap *pq) {
  struct tc *next = (struct tc *)heap_peek(pq);

  if (next) {
    return next->ss.pass;
  } else {
    return 0;
  }
}

void tc_join(struct tc *c) {
  DCHECK(!c->state.queued);
  DCHECK(!c->state.runnable);

  c->state.runnable = 1;

  if (!c->state.throttled) {
    struct pgroup *g = c->ss.my_pgroup;
    struct heap *pq = &g->pq;

    c->state.queued = 1;
    c->ss.pass = next_pass(pq) + c->ss.remain;
    heap_push(pq, c->ss.pass, c);
    tc_inc_refcnt(c);
  }
}

void tc_leave(struct tc *c) {
  /* if not joined yet, do nothing */
  if (c->state.runnable) {
    struct pgroup *g = c->ss.my_pgroup;
    struct heap *pq = &g->pq;

    c->state.runnable = 0;
    c->ss.remain = c->ss.pass - next_pass(pq);
  }
}

struct sched *sched_init() {
  struct sched *s;

  s = (struct sched *)mem_alloc(sizeof(*s));
  if (!s) {
    abort();
  }

  s->root.refcnt = 1;
  cdlist_head_init(&s->root.tasks); /* this will be always empty */
  cdlist_head_init(&s->root.pgroups);

  heap_init(&s->pq);

  cdlist_head_init(&s->tcs_all);

  return s;
}

/* Deallocate the scheduler. The owner is still responsible to release
 * its references to all traffic classes */
void sched_free(struct sched *s) {
  struct tc *c;
  struct tc *next;

  cdlist_for_each_entry_safe(c, next, &s->tcs_all, sched_all) {
    int queued = c->state.queued;
    int throttled = c->state.throttled;

    if (queued) {
      c->state.queued = 0;
      tc_dec_refcnt(c);
    }

    if (throttled) {
      c->state.throttled = 0;
      tc_dec_refcnt(c);
    }
  }

  heap_close(&s->pq);

  /* the actual memory block of s will be freed by the root TC
   * since it shares the address with this scheduler */
  tc_dec_refcnt(&s->root);
}

static void resume_throttled(struct sched *s, uint64_t tsc) {
  while (s->pq.num_nodes > 0) {
    struct tc *c;
    int64_t event_tsc;

    heap_peek_valdata(&s->pq, &event_tsc, (void **)&c);

    if ((uint64_t)event_tsc > tsc) {
      break;
    }

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
static struct tc *pick(struct tc *c) {
  struct pgroup *g;

again:
  /* found a leaf? */
  if (cdlist_is_empty(&c->pgroups)) {
    return c;
  }

  cdlist_for_each_entry(g, &c->pgroups, tc) {
    struct heap *pq = &g->pq;
    struct tc *child;

    child = (struct tc *)heap_peek(pq);
    if (!child) {
      continue;
    }

    DCHECK(child->state.queued);

    if (!child->state.runnable) {
      return child;
    }

    c = child;
    goto again;
  }

  return nullptr;
}

static struct tc *sched_next(struct sched *s, uint64_t tsc) {
  struct tc *c;

  DCHECK(!s->current);

  resume_throttled(s, tsc);

again:
  c = pick(&s->root);

  /* empty tree? */
  if (c == &s->root) {
    c = nullptr;
  }

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
static inline void accumulate(resource_arr_t acc, resource_arr_t x) {
  uint64_t *p1 = acc;
  uint64_t *p2 = x;

#if 0 && __AVX2__
	*((__m256i *)p1) = _mm256_add_epi64(*((__m256i *)p1), *((__m256i *)p2));
#elif 0 && __AVX__
  *((__m128i *)p1 + 0) =
      _mm_add_epi64(*((__m128i *)p1 + 0), *((__m128i *)p2 + 0));
  *((__m128i *)p1 + 1) =
      _mm_add_epi64(*((__m128i *)p1 + 1), *((__m128i *)p2 + 1));
#else
  for (int i = 0; i < NUM_RESOURCES; i++) {
    p1[i] += p2[i];
  }
#endif
}

/* returns 1 if it has been throttled */
static int tc_account(struct sched *s, struct tc *c, resource_arr_t usage,
                      uint64_t tsc) {
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

    if (!limit) {
      continue;
    }

    consumed = usage[i] << USAGE_AMPLIFIER_POW;
    tokens = c->tb[i].tokens + limit * elapsed_cycles;

    if (tokens < consumed) {
      uint64_t wait_tsc;

      wait_tsc = (consumed - tokens) / limit;

      throttled = 1;

      if (wait_tsc > max_wait_tsc) {
        max_wait_tsc = wait_tsc;
      }
    } else {
      c->tb[i].tokens = std::min(tokens - consumed, c->tb[i].max_burst);
    }
  }

  if (throttled) {
    for (i = 0; i < NUM_RESOURCES; i++) {
      c->tb[i].tokens = 0;
    }

    c->state.throttled = 1;
    c->stats.cnt_throttled++;

    heap_push(&s->pq, tsc + max_wait_tsc, c);
    tc_inc_refcnt(c);

    return 1;
  }

  return 0;
}

/* must be called after the previous sched_next() */
static void sched_done(struct sched *s, struct tc *c, resource_arr_t usage,
                       int reschedule, uint64_t tsc) {
  accumulate(s->stats.usage, usage);

  DCHECK(s->current);
  s->current = nullptr;

  if (!reschedule) {
    c->state.runnable = 0;
  }

  /* upwards from the leaf, skipping the root class */
  do {
    struct pgroup *g = c->ss.my_pgroup;
    struct heap *pq = &g->pq;

    uint64_t consumed = usage[g->resource];

    int throttled;

    DCHECK(c->state.queued);
    c->ss.pass += c->ss.stride * consumed / QUANTUM;

    throttled = tc_account(s, c, usage, tsc);
    if (throttled) {
      reschedule = 0;
    }

    if (reschedule) {
      heap_replace(pq, c->ss.pass, c);
    } else {
      c->state.queued = 0;
      heap_pop(pq);
      tc_dec_refcnt(c);

      c->ss.remain = c->ss.pass - next_pass(pq);

      reschedule = (pq->num_nodes > 0);
    }

    c = c->parent;
  } while (!tc_is_root(c));
}

/* print out all resource usage fields */
static char *print_tc_stats_detail(struct sched *s, char *p, int max_cnt) {
  const char *fields[] = {
      "count", "cycles", "packets", "bits", "throttled",
  };

  const int num_fields = sizeof(fields) / sizeof(sizeof(const char *));

  struct tc *c;

  int num_printed;

  static_assert(sizeof(struct tc_stats) >= sizeof(fields),
                "incomplete field names");

  p += sprintf(p, "\n");

  if (cdlist_is_empty(&s->tcs_all)) {
    return p;
  }

  p += sprintf(p, "%-10s ", "TC");
  num_printed = 0;

  cdlist_for_each_entry(c, &s->tcs_all, sched_all) {
    if (num_printed < max_cnt) {
      p += sprintf(p, "%12s", c->settings.name.c_str());
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

#define COUNTER (((uint64_t *)&c->stats)[i])
#define LAST_COUNTER (((uint64_t *)&c->last_stats)[i])

      value = COUNTER - LAST_COUNTER;
      LAST_COUNTER = COUNTER;

#undef COUNTER
#undef LAST_COUNTER

      if (num_printed < max_cnt) {
        p += sprintf(p, "%12" PRIu64, value);
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

#if 0
static char *print_tc_stats_simple(struct sched *s, char *p, int max_cnt) {
  struct tc *c;

  int num_printed = 0;

  cdlist_for_each_entry(c, &s->tcs_all, sched_all) {
    uint64_t cnt;
    uint64_t cycles;
    uint64_t pkts;
    uint64_t bits;

    cnt = c->stats.usage[RESOURCE_CNT] - c->last_stats.usage[RESOURCE_CNT];

    cycles =
        c->stats.usage[RESOURCE_CYCLE] - c->last_stats.usage[RESOURCE_CYCLE];

    pkts =
        c->stats.usage[RESOURCE_PACKET] - c->last_stats.usage[RESOURCE_PACKET];

    bits = c->stats.usage[RESOURCE_BIT] - c->last_stats.usage[RESOURCE_BIT];

    c->last_stats = c->stats;

    p += sprintf(p, "\tC%s %.1f%%(%.2fM) %.3fMpps %.1fMbps", c->settings.name.c_str(),
                 cycles * 100.0 / tsc_hz, cnt / 1000000.0, pkts / 1000000.0,
                 bits / 1000000.0);

    num_printed++;

    if (num_printed >= max_cnt) {
      p += sprintf(p, "\t... (%d more)", s->num_classes - max_cnt);
      break;
    }
  }

  p += sprintf(p, "\n");

  return p;
}
#endif

static void print_stats(struct sched *s, struct sched_stats *last_stats) {
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

  cnt = s->stats.usage[RESOURCE_CNT] - last_stats->usage[RESOURCE_CNT];

  cycles = s->stats.usage[RESOURCE_CYCLE] - last_stats->usage[RESOURCE_CYCLE];

  pkts = s->stats.usage[RESOURCE_PACKET] - last_stats->usage[RESOURCE_PACKET];

  bits = s->stats.usage[RESOURCE_BIT] - last_stats->usage[RESOURCE_BIT];

  p = buf;
  p += sprintf(p,
               "W%d: idle %.1f%%(%.1fM) "
               "total %.1f%%(%.1fM) %.3fMpps %.1fMbps ",
               ctx.wid(), cycles_idle * 100.0 / tsc_hz, cnt_idle / 1000000.0,
               cycles * 100.0 / tsc_hz, cnt / 1000000.0, pkts / 1000000.0,
               bits / 1000000.0);

#if 0
  p = print_tc_stats_simple(s, p, 5);
#else
  p = print_tc_stats_detail(s, p, 16);
#endif

  fprintf(stderr, "%s\n", buf);
}

static inline struct task_result tc_scheduled(struct tc *c) {
  struct task_result ret;
  struct task *t;

  int num_tasks = c->num_tasks;

  while (num_tasks--) {
    t = container_of(cdlist_rotate_left(&c->tasks), struct task, tc);

    ret = task_scheduled(t);
    if (ret.packets) {
      return ret;
    }
  }

  return (struct task_result){.packets = 0, .bits = 0};
}

// Thread local variables that we only want to initialize upon sched_loop
// getting invoked.
static thread_local struct sched_stats last_stats;
static thread_local uint64_t last_print_tsc;
static thread_local uint64_t checkpoint;
static thread_local uint64_t now;

void print_last_stats(struct sched *s) {
  print_stats(s, &last_stats);
}

void schedule_once(struct sched *s) {
  static const double ns_per_cycle = 1e9 / tsc_hz;

  /* Schedule (S) */
  struct tc *c = sched_next(s, now);

  if (c) {
    /* Running (R) */
    ctx.set_current_tsc(now); /* tasks see updated tsc */
    ctx.set_current_ns(now * ns_per_cycle);
    struct task_result ret = tc_scheduled(c);

    now = rdtsc();

    /* Accounting (A) */
    resource_arr_t usage;
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

void sched_loop(struct sched *s) {
  // How many rounds to go before we do accounting.
  const uint64_t accounting_mask = 0xff;
  static_assert(((accounting_mask + 1) & (accounting_mask)) == 0,
                "Accounting mask must be a (2^n)-1");

  last_stats = s->stats;
  last_print_tsc = checkpoint = now = rdtsc();

  /* the main scheduling - running - accounting loop */
  for (uint64_t round = 0;; round++) {
    /* periodic check for every 2^8 rounds,
     * to mitigate expensive operations */
    if ((round & accounting_mask) == 0) {
      if (unlikely(ctx.is_pause_requested())) {
        if (unlikely(ctx.Block())) {
          // TODO(barath): Add log message here?
          break;
        }
        last_stats = s->stats;
        last_print_tsc = checkpoint = now = rdtsc();
      } else if (unlikely(FLAGS_s && now - last_print_tsc >= tsc_hz)) {
        print_stats(s, &last_stats);
        last_stats = s->stats;
        last_print_tsc = checkpoint = now = rdtsc();
      }
    }

    schedule_once(s);
  }
}
