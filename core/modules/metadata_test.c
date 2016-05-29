#include "../module.h"

static struct snobj *
add_fields(struct module *m, struct snobj *fields, enum metadata_mode mode)
{
	if (snobj_type(fields) != TYPE_MAP)
		return snobj_err(EINVAL, 
				"argument must be a map of "
				"{'field name': size, ...}");

	/* a bit hacky, since there is no iterator for maps... */
	for (int i = 0; i < fields->size; i++) {
		int ret;

		const char *field_name = fields->map.arr_k[i];
		int field_size = snobj_int_get((fields->map.arr_v[i]));

		ret = register_metadata_field(m, field_name, field_size, mode);
		if (ret < 0)
			return snobj_err(-ret, "invalid metadata declaration");

		/* check /var/log/syslog for log messages */
		switch (mode) {
		case READ:
			log_info("module %s: %s, %d bytes, %s\n", 
				m->name, field_name, field_size, "read");
			break;
		case WRITE:
			log_info("module %s: %s, %d bytes, %s\n", 
				m->name, field_name, field_size, "write");
			break;
		case UPDATE:
			log_info("module %s: %s, %d bytes, %s\n", 
				m->name, field_name, field_size, "update");
			break;
		}
	}

	return NULL;
}

static struct snobj *metadata_test_init(struct module *m, struct snobj *arg)
{
	struct snobj *fields;
	struct snobj *err;

	if ((fields = snobj_eval(arg, "read"))) {
		err = add_fields(m, fields, READ);
		if (err)
			return err;
	}
	
	if ((fields = snobj_eval(arg, "write"))) {
		err = add_fields(m, fields, WRITE);
		if (err)
			return err;
	}

	if ((fields = snobj_eval(arg, "update"))) {
		err = add_fields(m, fields, UPDATE);
		if (err)
			return err;
	}

	return NULL;
}

static void 
metadata_test_process_batch(struct module *m, struct pkt_batch *batch)
{
	/* This module simply passes packets from input gate X down 
	 * to output gate X (the same gate index) */
	run_choose_module(m, get_igate(), batch);
}

static const struct mclass metadata_test = {
	.name 			= "MetadataTest",
	.def_module_name 	= "mt_test",
	.help			= "Dynamic metadata test module",
	.num_igates		= MAX_GATES,
	.num_ogates		= MAX_GATES,
	.init 			= metadata_test_init,
	.process_batch		= metadata_test_process_batch,
};

ADD_MCLASS(metadata_test)
