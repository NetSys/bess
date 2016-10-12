#include <string.h>

#include "log.h"
#include "namespace.h"
#include "module.h"

size_t list_mclasses(const ModuleClass **p_arr, size_t arr_size,
		size_t offset)
{
	size_t ret = 0;
	size_t iter_cnt = 0;

	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_MCLASS);
	while (1) {
		ModuleClass *mc_obj = (ModuleClass *) ns_next(&iter);
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

const ModuleClass *find_mclass(const char *name)
{
	return (ModuleClass *) ns_lookup(NS_TYPE_MCLASS, name);
}

#if 0
int is_valid_attr_list(const ModuleClass *mclass)
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

int add_mclass(const ModuleClass *mclass)
{
	int ret;

	/* already exists? */
	if (ns_name_exists(mclass->Name()))
		return 0;
#if 0
	if (!is_valid_attr_list(mclass)) {
		log_err("is_valid_attr_list() failure for module class '%s'\n",
				mclass->name);
		return -1;
	}
#endif
	ret = ns_insert(NS_TYPE_MCLASS, mclass->Name(), (void *) mclass);
	if (ret < 0) {
		log_err("ns_insert() failure for module class '%s'\n",
				mclass->Name());
		return -1;
	}

	log_debug("Module class '%s' has been registered", mclass->class_name);
#if 0
	if (mclass->priv_size)
		log_debug(", with %u-byte private data", mclass->priv_size);
#endif

	log_debug("\n");

	return 0;
}
#endif
