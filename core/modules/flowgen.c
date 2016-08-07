#include <math.h>

#include "../utils/random.h"
#include "../time.h"

#include "../module.h"

#define MAX_TEMPLATE_SIZE	1536

#define RETRY_NS		1000000ul	/* 1 ms */

struct flow {
	uint32_t flow_id;
	int packets_left;
	int first;
	struct cdlist_item free;
};

struct flowgen_priv {
	int active_flows;
	int allocated_flows;
	uint64_t generated_flows;
	struct flow *flows;
	struct cdlist_head flows_free;

	struct heap events;

	char template[MAX_TEMPLATE_SIZE];
	int template_size;

	uint64_t rseed;

	/* behavior parameters */
	int quick_rampup;

	enum {
		arrival_uniform = 0,
		arrival_exponential,
	} arrival;

	enum {
		duration_uniform = 0,
		duration_pareto,
	} duration;

	/* load parameters */
	double total_pps;
	double flow_rate;		/* in flows/s */
	double flow_duration;		/* in seconds */

	/* derived variables */
	double concurrent_flows;	/* expected # of flows */
	double flow_pps;		/* packets/s/flow */
	double flow_pkts;		/* flow_pps * flow_duration */
	double flow_gap_ns;		/* == 10^9 / flow_rate */

	struct {
		double alpha;
		double inversed_alpha; 	/* 1.0 / alpha */
		double mean;		/* determined by alpha */
	} pareto;
};

/* we ignore the last 1% tail to make the variance finite */
const double pareto_tail_limit = 0.99;

/* find x from CDF of pareto distribution from given y in [0.0, 1.0) */
static inline double pareto_variate(double inversed_alpha, double y)
{
	return pow(1.0 / (1.0 - y * pareto_tail_limit), inversed_alpha);
}

static inline double
scaled_pareto_variate(double inversed_alpha, double mean, double desired_mean,
		double y)
{
	double x = pareto_variate(inversed_alpha, y);

	return 1.0 + (x - 1.0) / (mean - 1.0) * (desired_mean - 1.0);
}

static inline double new_flow_pkts(struct flowgen_priv *priv)
{
	switch (priv->duration) {
	case duration_uniform:
		return priv->flow_pkts;
	case duration_pareto:
		return scaled_pareto_variate(priv->pareto.inversed_alpha,
				priv->pareto.mean, priv->flow_pkts,
				rand_fast_real(&priv->rseed));
	default:
		assert(0);
	}
}

static inline double max_flow_pkts(const struct flowgen_priv *priv)
{
	switch (priv->duration) {
	case duration_uniform:
		return priv->flow_pkts;
	case duration_pareto:
		return scaled_pareto_variate(priv->pareto.inversed_alpha,
				priv->pareto.mean, priv->flow_pkts,
				1.0);
	default:
		assert(0);
	}
}

static inline uint64_t next_flow_arrival(struct flowgen_priv *priv)
{
	switch (priv->arrival) {
	case arrival_uniform:
		return priv->flow_gap_ns;
		break;
	case arrival_exponential:
		return -log(rand_fast_real2(&priv->rseed)) * priv->flow_gap_ns;
		break;
	default:
		assert(0);
	}
}

static inline struct flow *
schedule_flow(struct flowgen_priv *priv, uint64_t time_ns)
{
	struct cdlist_item *item;
	struct flow *f;

	item = cdlist_pop_head(&priv->flows_free);
	if (!item)
		return NULL;

	f = container_of(item, struct flow, free);
	f->first = 1;
	f->flow_id = (uint32_t)rand_fast(&priv->rseed);

	/* compensate the fraction part by adding [0.0, 1.0) */
	f->packets_left = new_flow_pkts(priv) + rand_fast_real(&priv->rseed);;

	priv->active_flows++;
	priv->generated_flows++;

	heap_push(&priv->events, time_ns, f);

	return f;
}

static void measure_pareto_mean(struct flowgen_priv *priv)
{
	const int iteration = 1000000;
	double total = 0.0;

	for (int i = 0; i <= iteration; i++) {
		double y = i / (double)iteration;
		double x = pareto_variate(priv->pareto.inversed_alpha, y);
		total += x;
	}

	priv->pareto.mean = total / (iteration + 1);
}

static void populate_initial_flows(struct flowgen_priv *priv)
{
	/* cannot use ctx.current_ns in the master thread... */
	uint64_t now_ns = rdtsc() / tsc_hz * 1e9;
	struct flow *f;

	f = schedule_flow(priv, now_ns);
	assert(f);

	if (!priv->quick_rampup)
		return;

	if (priv->flow_pps < 1.0 || priv->flow_rate < 1.0)
		return;

	/* emulate pre-existing flows at the beginning */
	double past_origin = max_flow_pkts(priv) / priv->flow_pps; /* in secs */
	double step = 1.0 / priv->flow_rate;

	for (double past = step; past < past_origin; past += step) {
		double pre_consumed_pkts = priv->flow_pps * past;
		double flow_pkts = new_flow_pkts(priv);

		if (flow_pkts > pre_consumed_pkts) {
			uint64_t jitter = 1e9 * rand_fast_real(&priv->rseed) /
				priv->flow_pps;

			f = schedule_flow(priv, now_ns + jitter);
			if (!f)
				break;

			/* overwrite with a emulated pre-existing flow */
			f->first = 0;
			f->packets_left = flow_pkts - pre_consumed_pkts;
		}
	}
}

static struct snobj *
process_arguments(struct flowgen_priv *priv, struct snobj *arg)
{
	struct snobj *t;

	if (!arg || !(t = snobj_eval(arg, "template")))
		return snobj_err(EINVAL, "must specify 'template'");

	if (snobj_type(t) != TYPE_BLOB)
		return snobj_err(EINVAL, "'template' must be BLOB type");

	if (snobj_size(t) > MAX_TEMPLATE_SIZE)
		return snobj_err(EINVAL, "'template' is too big");

	priv->template_size = snobj_size(t);

	memset(priv->template, 0, MAX_TEMPLATE_SIZE);
	memcpy(priv->template, snobj_blob_get(t), priv->template_size);

	if ((t = snobj_eval(arg, "pps")) != NULL) {
		priv->total_pps = snobj_number_get(t);
		if (isnan(priv->total_pps) || priv->total_pps < 0.0)
			return snobj_err(EINVAL, "invalid 'pps'");
	}

	if ((t = snobj_eval(arg, "flow_rate")) != NULL) {
		priv->flow_rate = snobj_number_get(t);
		if (isnan(priv->flow_rate) || priv->flow_rate < 0.0)
			return snobj_err(EINVAL, "invalid 'flow_rate'");
	}

	if ((t = snobj_eval(arg, "flow_duration")) != NULL) {
		priv->flow_duration = snobj_number_get(t);
		if (isnan(priv->flow_duration) || priv->flow_duration < 0.0)
			return snobj_err(EINVAL, "invalid 'flow_duration'");
	}

	if ((t = snobj_eval(arg, "arrival")) != NULL) {
		if (strcmp(snobj_str_get(t), "uniform") == 0)
			priv->arrival = arrival_uniform;
		else if (strcmp(snobj_str_get(t), "exponential") == 0)
			priv->arrival = arrival_exponential;
		else
			return snobj_err(EINVAL, "'arrival' must be either "
					"'uniform' or 'exponential'");
	}

	if ((t = snobj_eval(arg, "duration")) != NULL) {
		if (strcmp(snobj_str_get(t), "uniform") == 0)
			priv->duration = duration_uniform;
		else if (strcmp(snobj_str_get(t), "pareto") == 0)
			priv->duration = duration_pareto;
		else
			return snobj_err(EINVAL, "'duration' must be either "
					"'uniform' or 'pareto'");
	}

	if (snobj_eval_int(arg, "quick_rampup"))
		priv->quick_rampup = 1;

	return NULL;
}

static struct snobj *
init_flow_pool(struct flowgen_priv *priv)
{
	/* allocate 20% more in case of temporal overflow */
	priv->allocated_flows = (int)(priv->concurrent_flows * 1.2);
	if (priv->allocated_flows < 128)
		priv->allocated_flows = 128;

	priv->flows = mem_alloc(priv->allocated_flows * sizeof(struct flow));
	if (!priv->flows)
		return snobj_err(ENOMEM, "memory allocation failed (%d flows)",
				priv->allocated_flows);

	cdlist_head_init(&priv->flows_free);

	for (int i = 0; i < priv->allocated_flows; i++) {
		struct flow *f = &priv->flows[i];
		cdlist_add_tail(&priv->flows_free, &f->free);
	}

	return NULL;
}

static struct snobj *flowgen_init(struct module *m, struct snobj *arg)
{
	struct flowgen_priv *priv = get_priv(m);

	task_id_t tid;
	struct snobj *err;

	priv->rseed = 0xBAADF00DDEADBEEFul;

	/* set default parameters */
	priv->total_pps = 1000.0;
	priv->flow_rate = 10.0;
	priv->flow_duration = 10.0;
	priv->arrival = arrival_uniform;
	priv->duration = duration_uniform;
	priv->pareto.alpha = 1.3;

	/* register task */
	tid = register_task(m, NULL);
	if (tid == INVALID_TASK_ID)
		return snobj_err(ENOMEM, "task creation failed");

	err = process_arguments(priv, arg);
	if (err)
		return err;

	/* calculate derived variables */
	priv->pareto.inversed_alpha = 1.0 / priv->pareto.alpha;

	if (priv->duration == duration_pareto)
		measure_pareto_mean(priv);

	priv->concurrent_flows = priv->flow_rate * priv->flow_duration;
	if (priv->concurrent_flows > 0.0)
		priv->flow_pps = priv->total_pps / priv->concurrent_flows;

	priv->flow_pkts = priv->flow_pps * priv->flow_duration;
	if (priv->flow_rate > 0.0)
		priv->flow_gap_ns = 1e9 / priv->flow_rate;

	/* initialize flow pool */
	err = init_flow_pool(priv);
	if (err)
		return err;

	/* initialize time-sorted priority queue */
	heap_init(&priv->events);

	/* add a seed flow (and background flows if necessary) */
	populate_initial_flows(priv);

	return NULL;
}

static void flowgen_deinit(struct module *m)
{
	struct flowgen_priv *priv = get_priv(m);

	mem_free(priv->flows);
	heap_close(&priv->events);
}

static struct snbuf *fill_packet(struct flowgen_priv *priv, struct flow *f)
{
	struct snbuf *pkt;
	char *p;

	uint8_t tcp_flags;

	int size = priv->template_size;

	if (!(pkt = snb_alloc()))
		return NULL;

	p = pkt->mbuf.buf_addr + SNBUF_HEADROOM;

	pkt->mbuf.data_off = SNBUF_HEADROOM;
	pkt->mbuf.pkt_len = size;
	pkt->mbuf.data_len = size;

	memcpy_sloppy(p, priv->template, size);

	tcp_flags = f->first ? /* SYN */ 0x02 : /* ACK */ 0x10;

	if (f->packets_left <= 1)
		tcp_flags |= 0x01;		/* FIN */

	*(uint32_t *)(p + 14 + /* IP dst */ 16) = f->flow_id;
	*(uint8_t *)(p + 14 + /* IP */ 20 + /* TCP flags */ 13) = tcp_flags;

	return pkt;
}

static void
generate_packets(struct flowgen_priv *priv, struct pkt_batch *batch)
{
	uint64_t now = ctx.current_ns;

	struct heap *h = &priv->events;

	batch_clear(batch);

	while (!batch_full(batch)) {
		uint64_t t;
		struct flow *f;
		struct snbuf *pkt;

		heap_peek_valdata(h, (int64_t *)&t, (void **)&f);
		if (!f || now < t)
			return;

		heap_pop(h);

		if (f->packets_left <= 0) {
			cdlist_add_head(&priv->flows_free, &f->free);
			priv->active_flows--;
			continue;
		}

		pkt = fill_packet(priv, f);

		if (f->first) {
			uint64_t delay_ns = next_flow_arrival(priv);
			struct flow *new_f;

			new_f = schedule_flow(priv, t + delay_ns);
			if (!new_f) {
				/* temporarily out of free flow data. retry. */
				heap_push(h, t + RETRY_NS, f);
				continue;
			}

			f->first = 0;
		}

		f->packets_left--;

		heap_push(h, t + (uint64_t)(1e9 / priv->flow_pps), f);

		if (pkt)
			batch_add(batch, pkt);
	}
}

static struct task_result
flowgen_run_task(struct module *m, void *arg)
{
	struct flowgen_priv *priv = get_priv(m);

	struct pkt_batch batch;
	struct task_result ret;

	const int pkt_overhead = 24;

	generate_packets(priv, &batch);
	if (batch.cnt > 0)
		run_next_module(m, &batch);

	ret = (struct task_result) {
		.packets = batch.cnt,
		.bits = ((priv->template_size + pkt_overhead) * batch.cnt) * 8,
	};

	return ret;
}

static struct snobj *flowgen_get_desc(const struct module *m)
{
	const struct flowgen_priv *priv = get_priv_const(m);

	return snobj_str_fmt("%d flows", priv->active_flows);
}

static struct snobj *flowgen_get_dump(const struct module *m)
{
	const struct flowgen_priv *priv = get_priv_const(m);

	struct snobj *r = snobj_map();

	{
		struct snobj *t = snobj_map();

		snobj_map_set(t, "allocated_flows",
				snobj_int(priv->allocated_flows));
		snobj_map_set(t, "active_flows",
				snobj_int(priv->active_flows));
		snobj_map_set(t, "generated_flows",
				snobj_int(priv->generated_flows));

		snobj_map_set(r, "stats", t);
	}

	{
		struct snobj *t = snobj_map();

		snobj_map_set(t, "total_pps",
				snobj_double(priv->total_pps));
		snobj_map_set(t, "flow_rate",
				snobj_double(priv->flow_rate));
		snobj_map_set(t, "flow_duration",
				snobj_double(priv->flow_duration));

		snobj_map_set(r, "load", t);
	}

	{
		struct snobj *t = snobj_map();

		snobj_map_set(t, "concurrent_flows",
				snobj_double(priv->concurrent_flows));
		snobj_map_set(t, "flow_pps",
				snobj_double(priv->flow_pps));
		snobj_map_set(t, "flow_pkts",
				snobj_double(priv->flow_pkts));
		snobj_map_set(t, "flow_gap_ns",
				snobj_double(priv->flow_gap_ns));

		snobj_map_set(r, "derived", t);
	}

	{
		struct snobj *t = snobj_map();

		snobj_map_set(t, "quick_rampup", snobj_int(priv->quick_rampup));
		snobj_map_set(t, "arrival",
				snobj_str(priv->arrival == arrival_uniform ?
					"uniform" : "exponential"));
		snobj_map_set(t, "duration",
				snobj_str(priv->duration == duration_uniform ?
					"uniform" : "pareto"));

		snobj_map_set(r, "behavior", t);
	}

	if (priv->duration == duration_pareto) {
		struct snobj *t = snobj_map();

		snobj_map_set(t, "alpha",
				snobj_double(priv->pareto.alpha));
		snobj_map_set(t, "mean",
				snobj_double(priv->pareto.mean));
		snobj_map_set(t, "max",
				snobj_int(max_flow_pkts(priv)));

		snobj_map_set(r, "pareto", t);
	}

	return r;
}

static const struct mclass flowgen = {
	.name 		= "FlowGen",
	.help		= "generates packets on a flow basis",
	.num_igates	= 0,
	.num_ogates	= 1,
	.priv_size	= sizeof(struct flowgen_priv),
	.init 		= flowgen_init,
	.deinit 	= flowgen_deinit,
	.run_task 	= flowgen_run_task,
	.get_desc	= flowgen_get_desc,
	.get_dump	= flowgen_get_dump,
};

ADD_MCLASS(flowgen)
