#include "port.h"

size_t list_drivers(const Driver **p_arr, size_t arr_size, size_t offset)
{
	size_t ret = 0;
	size_t iter_cnt = 0;

	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_DRIVER);
	while (1) {
		Driver* driver = (Driver *) ns_next(&iter);
		if (!driver)
			break;

		if (iter_cnt++ < offset)
			continue;

		if (ret >= arr_size)
			break;

		p_arr[ret++] = driver;
	}
	ns_release_iterator(&iter);

	return ret;
}

const Driver *find_driver(const char *name)
{
	return (Driver *) ns_lookup(NS_TYPE_DRIVER, name);
}

#if 0
int add_driver(const Driver *driver)
{
	int ret;

	if (!driver->name || !driver->init_port) {
		log_err("Port driver %s is incomplete\n", 
				driver->name ?: "<noname>"); 
		return -1;
	}

	/* already exists? */
	if (find_driver(driver->name))
		return 0;

	ret = ns_insert(NS_TYPE_DRIVER, driver->name, (void *) driver);
	if (ret < 0) {
		log_err("Failed to add driver '%s'\n", driver->name);
		return ret;
	}

	log_debug("Port driver '%s' has been registered", driver->name);
	if (driver->priv_size)
		log_debug(", with %zu-byte private data", driver->priv_size);
	log_debug("\n");

	return 0;
}
#endif

void init_drivers()
{
	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_DRIVER);
	while (1) {
		Driver *driver = (Driver *) ns_next(&iter);
		if (!driver)
			break;

		driver->Init();
	}
	ns_release_iterator(&iter);
}
