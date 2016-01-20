#include "log.h"
#include "namespace.h"
#include "mclass.h"

size_t list_mclasses(const struct mclass **p_arr, size_t arr_size, 
		size_t offset)
{
	int ret = 0; 
	int iter_cnt = 0;

	struct ns_iter iter;
	
	ns_init_iterator(&iter, NS_TYPE_MCLASS);
	while (1) {
		struct mclass *mc_obj = (struct mclass *) ns_next(&iter);
		if (!mc_obj)
			break;

		if (iter_cnt++ < offset)
			continue;

		if (ret >= arr_size)
			break;
		
		p_arr[ret++] = mc_obj;

	}
	ns_release_iterator(&iter);
	
	return ret;
}

const struct mclass *find_mclass(const char *name)
{
	return (struct mclass *) ns_lookup(NS_TYPE_MCLASS, name);
}

int add_mclass(const struct mclass *mclass)
{
	int ret;

	if (!mclass->name) {
		log_err("Incomplete module class at %p\n", mclass);
		return -1;
	}

	/* already exists? */
	if (ns_name_exists(mclass->name))
		return 0;
	
	ret = ns_insert(NS_TYPE_MCLASS, mclass->name, (void *) mclass);
	if (ret < 0) {
		log_err("Failed to add module class '%s'\n", mclass->name);
		return -1;
	}

	log_debug("Module class '%s' has been registered", mclass->name);
	if (mclass->priv_size)
		log_debug(", with %u-byte private data", mclass->priv_size);

	log_debug("\n");

	return 0;
}
