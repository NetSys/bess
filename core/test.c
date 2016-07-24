#include <time.h>
#include <string.h>

#include "log.h"

#include "test.h"

static struct testcase *head;
static struct testcase *tail;

static int num_tests;
static int num_forced_tests;

void add_testcase(struct testcase *t)
{
	if (!head)
		head = t;

	if (tail)
		tail->next = t;

	tail = t;

	num_tests++;
	if (t->forced)
		num_forced_tests++;
}

void run_tests()
{
	int i = 0;
	time_t curr;
	char buf[1024];

	if (num_tests == 0)
		return;

	time(&curr);
	ctime_r(&curr, buf);
	*strchr(buf, '\n') = '\0';

	log_notice("Test started at %s   --------------------------\n", buf);
	for (struct testcase *ptr = head; ptr; ptr = ptr->next) {
		i++;
		log_notice("%2d/%2d: %s\n", i, num_tests, ptr->name);
		ptr->func();
	}

	time(&curr);
	ctime_r(&curr, buf);
	*strchr(buf, '\n') = '\0';

	log_notice("Test ended at %s     --------------------------\n", buf);
}

void run_forced_tests()
{
	int i = 0;
	time_t curr;
	char buf[1024];

	if (num_forced_tests == 0)
		return;

	time(&curr);
	ctime_r(&curr, buf);
	*strchr(buf, '\n') = '\0';

	log_notice("Test started at %s   --------------------------\n", buf);

	for (struct testcase *ptr = head; ptr; ptr = ptr->next) {
		if (!ptr->forced)
			continue;
		i++;
		log_notice("%2d/%2d: %s\n", i, num_forced_tests, ptr->name);
		ptr->func();
	}

	time(&curr);
	ctime_r(&curr, buf);
	*strchr(buf, '\n') = '\0';

	log_notice("Test ended at %s     --------------------------\n", buf);
}
