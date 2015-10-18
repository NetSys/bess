#include "driver.h"
#include "namespace.h"

size_t list_drivers(const struct driver **p_arr, size_t arr_size, size_t offset)
{
	int ret = 0;
	int iter_cnt = 0;

	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_DRIVER);
	while (1) {
		struct driver* driver = (struct driver *) ns_next(&iter);
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

const struct driver *find_driver(const char *name)
{
	return (struct driver *) ns_lookup(NS_TYPE_DRIVER, name);
}

int add_driver(const struct driver *driver)
{
	int ret;

	if (!driver->name || !driver->init_port) {
		fprintf(stderr, "Port driver %s is incomplete\n", 
				driver->name ?: "<noname>"); 
		return -1;
	}

	/* already exists? */
	if (find_driver(driver->name))
		return 0;

	ret = ns_insert(NS_TYPE_DRIVER, driver->name, (void *) driver);
	if (ret < 0) {
		fprintf(stderr, "Fail to insert driver\n");
		return ret;
	}

	/*
	printf("Port driver '%s' has been registered", driver->name);
	if (driver->priv_size)
		printf(", with %zu bytes of private data", driver->priv_size);

	printf("\n");
	*/

	return 0;
}

void init_drivers()
{
	struct ns_iter iter;

	ns_init_iterator(&iter, NS_TYPE_DRIVER);
	while (1) {
		struct driver *driver = (struct driver *) ns_next(&iter);
		if (!driver)
			break;
		
		if (driver->init_driver)
			driver->init_driver(driver);
	}
	ns_release_iterator(&iter);
}
