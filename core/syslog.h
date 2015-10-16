#include <syslog.h>
#ifndef __SYSLOG_H__
#define __SYSLOG_H__
static ssize_t stdout_writer(void *cookie, const char *data, size_t len)
{
	syslog(LOG_INFO, "%.*s", (int)len, data);
	return  len;
}

static ssize_t stderr_writer(void *cookie, const char *data, size_t len)
{
	syslog(LOG_ERR, "%.*s", (int)len, data);
	return  len;
}

#define BESS_ID "bessd"
static void setup_syslog()
{
	cookie_io_functions_t stdout_funcs = {
		.write = &stdout_writer,
		.read = NULL,
		.close = NULL, 
		.seek = NULL
	};

	cookie_io_functions_t stderr_funcs = {
		.write = &stderr_writer,
		.read = NULL,
		.close = NULL, 
		.seek = NULL
	};

	openlog(BESS_ID, LOG_CONS | LOG_NDELAY, LOG_DAEMON);

	setvbuf(stderr = fopencookie(NULL, "w", stderr_funcs), NULL, _IOLBF, 0);

	setvbuf(stdout = fopencookie(NULL, "w", stdout_funcs), NULL, _IOLBF, 0);
}

static void end_syslog()
{
	closelog();
}
#endif
