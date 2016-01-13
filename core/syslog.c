#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/types.h>

#include <rte_hexdump.h>
#include "syslog.h"

#define BESS_ID "bessd"

static ssize_t stdout_writer(void *cookie, const char *data, size_t len)
{
	syslog(LOG_INFO, "%.*s", (int)len, data);
	return len;
}

static ssize_t stderr_writer(void *cookie, const char *data, size_t len)
{
	syslog(LOG_ERR, "%.*s", (int)len, data);
	return len;
}

void setup_syslog()
{
	const cookie_io_functions_t stdout_funcs = {
		.write = &stdout_writer,
	};

	const cookie_io_functions_t stderr_funcs = {
		.write = &stderr_writer,
	};

	int fd = open("/dev/null", O_RDWR, 0);
	if (fd >= 0) {   
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);

		if (fd > 2)
			close(fd);
	}

	openlog(BESS_ID, LOG_CONS | LOG_NDELAY, LOG_DAEMON);

	setvbuf(stdout = fopencookie(NULL, "w", stdout_funcs), NULL, _IOLBF, 0);
	setvbuf(stderr = fopencookie(NULL, "w", stderr_funcs), NULL, _IOLBF, 0);
}

void end_syslog()
{
	closelog();
}
