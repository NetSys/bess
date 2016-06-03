#include "../module.h"

static struct snobj *
add_attributes(struct module *m, struct snobj *attrs, enum mt_access_mode mode)
{
	if (snobj_type(attrs) != TYPE_MAP)
		return snobj_err(EINVAL, 
				"argument must be a map of "
				"{'attribute name': size, ...}");

	/* a bit hacky, since there is no iterator for maps... */
	for (int i = 0; i < attrs->size; i++) {
		int ret;

		const char *attr_name = attrs->map.arr_k[i];
		int attr_size = snobj_int_get((attrs->map.arr_v[i]));

		ret = add_metadata_attr(m, attr_name, attr_size, mode);
		if (ret < 0)
			return snobj_err(-ret, "invalid metadata declaration");

		/* check /var/log/syslog for log messages */
		switch (mode) {
		case MT_READ:
			log_info("module %s: %s, %d bytes, %s\n", 
				m->name, attr_name, attr_size, "read");
			break;
		case MT_WRITE:
			log_info("module %s: %s, %d bytes, %s\n", 
				m->name, attr_name, attr_size, "write");
			break;
		case MT_UPDATE:
			log_info("module %s: %s, %d bytes, %s\n", 
				m->name, attr_name, attr_size, "update");
			break;
		}
	}

	return NULL;
}

static struct snobj *metadata_test_init(struct module *m, struct snobj *arg)
{
	struct snobj *attrs;
	struct snobj *err;

	if ((attrs = snobj_eval(arg, "read"))) {
		err = add_attributes(m, attrs, MT_READ);
		if (err)
			return err;
	}
	
	if ((attrs = snobj_eval(arg, "write"))) {
		err = add_attributes(m, attrs, MT_WRITE);
		if (err)
			return err;
	}

	if ((attrs = snobj_eval(arg, "update"))) {
		err = add_attributes(m, attrs, MT_UPDATE);
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
