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

int is_valid_attr_list(const struct mclass *mclass)
{
	for (int i = 0; i < MAX_ATTRS_PER_MODULE; i++) {
		const struct mt_attr *a1 = &mclass->attrs[i];

		/* end of the list is marked as a empty-string name */
		if (strlen(a1->name) == 0) {
			/* there should be no hole in the list */
			for (int j = i + 1; j < MAX_ATTRS_PER_MODULE; j++) {
				const struct mt_attr *a2 = &mclass->attrs[j];
				if (strlen(a2->name) > 0)
					return 0;
			}

			return 1;
		}

		if (!is_valid_attr(a1->name, a1->size, a1->mode))
			return 0;

		/* duplicate attributes? */
		for (int j = 0; j < i - 1; j++) {
			const struct mt_attr *a2 = &mclass->attrs[j];
			if (strcmp(a1->name, a2->name) == 0)
				return 0;
		}
	}

	return 1;
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

	if (!is_valid_attr_list(mclass)) {
		log_err("is_valid_attr_list() failure for module class '%s'\n", 
				mclass->name);
		return -1;
	}

	ret = ns_insert(NS_TYPE_MCLASS, mclass->name, (void *) mclass);
	if (ret < 0) {
		log_err("ns_insert() failure for module class '%s'\n", 
				mclass->name);
		return -1;
	}

	log_debug("Module class '%s' has been registered", mclass->name);
	if (mclass->priv_size)
		log_debug(", with %u-byte private data", mclass->priv_size);

	log_debug("\n");

	return 0;
}
