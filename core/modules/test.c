#include "../module.h"

enum field_type {
	FIELD_READ,
	FIELD_WRITE
	/* not now: FIELD_UPDATE */
};

static struct snobj *
add_fields(struct module *m, struct snobj *fields, enum field_type t)
{
	if (snobj_type(fields) != TYPE_MAP)
		return snobj_err(EINVAL, 
				"argument must be a map of "
				"{'field name': size, ...}");

	/* a bit hacky, since there is no iterator for maps... */
	for (int i = 0; i < fields->size; i++) {
		const char *field_name = fields->map.arr_k[i];
		int field_size = snobj_int_get((fields->map.arr_v[i]));

		if (field_size < 1 || field_size > 16)
			return snobj_err(EINVAL,
					"invalid field size %d", field_size);

		int j = m->num_fields++;
		m->fields[j].name = strdup(field_name);
		m->fields[j].len = field_size;
		m->fields[j].mode = t;
		/* or with some form of API... */

		/* check /var/log/syslog for log messages */
		log_info("module %s: %s, %d bytes, %s\n", 
				m->name, field_name, field_size, 
				t == FIELD_READ ? "read" : "write");
	}

	return NULL;
}

static struct snobj *test_init(struct module *m, struct snobj *arg)
{
	struct snobj *fields;
	struct snobj *err;

	if ((fields = snobj_eval(arg, "read"))) {
		err = add_fields(m, fields, FIELD_READ);
		if (err)
			return err;
	}
	
	if ((fields = snobj_eval(arg, "write"))) {
		err = add_fields(m, fields, FIELD_WRITE);
		if (err)
			return err;
	}

	return NULL;
}

static void test_process_batch(struct module *m, struct pkt_batch *batch)
{
	/* This module simply passes packets from input gate X down 
	 * to output gate X (the same gate index) */
	run_choose_module(m, get_igate(), batch);
}

static const struct mclass test = {
	.name 		= "Test",
	//.help		= "Dynamic metadata test",
	.num_igates	= MAX_GATES,
	.num_ogates	= MAX_GATES,
	.init 		= test_init,
	.process_batch 	= test_process_batch,
};

ADD_MCLASS(test)
