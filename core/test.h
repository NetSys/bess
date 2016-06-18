#ifndef _TEST_H_
#define _TEST_H_

#define _ADD_TEST(_func, _name, _forced) \
	static struct testcase __t_##_func = { \
		.name = _name, \
		.func = _func, \
		.forced = _forced, \
	}; \
	__attribute__((constructor(104))) void __testcase_register_##_func() \
	{ \
		add_testcase(&__t_##_func); \
	}

#define ADD_TEST(func, name)		_ADD_TEST(func, name, 0)

/* convenience macro for testcase development */
#define ADD_TEST_FORCED(func, name)	_ADD_TEST(func, name, 1)

typedef void (*test_func_t)(void);

struct testcase {
	struct testcase *next;
	const char *name;
	int forced;
	test_func_t func;
};

void add_testcase(struct testcase *t);

void run_tests();
void run_forced_tests();

#endif
