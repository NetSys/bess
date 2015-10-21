#include "mclass.h"
#include "namespace.h"

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
		fprintf(stderr, "Incomplete module class at %p\n", mclass);
		return -1;
	}

	/* already exists? */
	if (ns_name_exists(mclass->name))
		return 0;
	
	ret = ns_insert(NS_TYPE_MCLASS, mclass->name, (void *) mclass);
	if (ret < 0) {
		fprintf(stderr, "Fail to insert module classes\n");
		return -1;
	}

	/*
	printf("Module class '%s' has been registered", mclass->name);
	if (mclass->priv_size)
		printf(", with %u bytes of private data", mclass->priv_size);

	printf("\n");
	*/

	return 0;
}
