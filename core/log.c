#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>

#include "common.h"
#include "opts.h"
#include "log.h"

#define BESS_ID "bessd"

#define MAX_LOG_PRIORITY	LOG_DEBUG	/* 7 */

#define ANSI_RED	"\x1b[31m"
#define ANSI_GREEN	"\x1b[32m"
#define ANSI_YELLOW	"\x1b[33m"
#define ANSI_BLUE	"\x1b[34m"
#define ANSI_MAGENTA	"\x1b[35m"
#define ANSI_CYAN	"\x1b[36m"
#define ANSI_RESET	"\x1b[0m"

static FILE *org_stdout;
static FILE *org_stderr;

struct logger {
	char buf[MAX_LOG_LEN * 2];
	int len;
};

static __thread struct logger loggers[MAX_LOG_PRIORITY + 1];

static int initialized = 0;

static void do_log(int priority, const char *data, size_t len)
{
	if (!initialized || global_opts.foreground) {
		FILE *fp;
		const char *color = NULL;

		if (priority <= LOG_ERR) {
			fp = org_stderr ? : stderr;
			color = ANSI_RED;
		} else {
			fp = org_stdout ? : stdout;
			if (priority <= LOG_NOTICE)
				color = ANSI_YELLOW;
		}

		if (color && isatty(fileno(fp)))
			fprintf(fp, "%s%.*s%s", 
					color, (int)len, data, ANSI_RESET);
		else
			fprintf(fp, "%.*s", (int)len, data);
	} else {
		syslog(priority, "%.*s", (int)len, data);
	}
}

static void log_flush(int priority, struct logger *logger, int forced)
{
	char *p;

	p = logger->buf;
	for (;;) {
		char *lf = strchr(p, '\n');
		if (!lf) {
			/* forced or a long line without LF? */
			if (forced || logger->len >= MAX_LOG_LEN) {
				char tmp = p[MAX_LOG_LEN + 1];

				p[MAX_LOG_LEN + 1] = '\n';
				do_log(priority, p, MAX_LOG_LEN + 1);
				p[MAX_LOG_LEN + 1] = tmp;

				p += MAX_LOG_LEN;
				logger->len -= MAX_LOG_LEN;
			}

			break;
		}

		do_log(priority, p, lf - p + 1);
		logger->len -= (lf - p + 1);
		p = lf + 1;
	}
		
	if (p != logger->buf && logger->len > 0)
		memmove(logger->buf, p, logger->len);
}

static void log_vfmt(int priority, const char *fmt, va_list ap)
{
	int free_space;
	int to_write;

	struct logger *logger = &loggers[priority];

	free_space = sizeof(logger->buf) - logger->len;

	to_write = vsnprintf(logger->buf + logger->len, free_space, fmt, ap);
	if (to_write >= free_space) {
		/* contract violated (to_write >= MAX_LOG_LEN) */
		char msg[BUFSIZ];
		size_t len;

		len = sprintf(msg, "Too large log message: %d bytes\n",
				to_write); 
		do_log(LOG_ERR, msg, len);

		return;
	}

	logger->len += to_write;

	if (initialized || priority < LOG_DEBUG)
		log_flush(priority, logger, 0);
}

void _log(int priority, const char *fmt, ...)
{
	va_list ap;

	if (priority < 0 || priority > MAX_LOG_PRIORITY)
		return;

	if (!initialized || global_opts.debug_mode || priority < LOG_DEBUG) {
		va_start(ap, fmt);
		log_vfmt(priority, fmt, ap);
		va_end(ap);
	}
}

void log_perr(const char *fmt, ...)
{
	char buf[MAX_LOG_LEN];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	_log(LOG_ERR, "%s: %m\n", buf);
}

static ssize_t stdout_writer(void *cookie, const char *data, size_t len)
{
	_log(LOG_INFO, "%.*s", (int)len, data);
	return len;
}

static ssize_t stderr_writer(void *cookie, const char *data, size_t len)
{
	_log(LOG_ERR, "%.*s", (int)len, data);
	return len;
}

static cookie_io_functions_t stdout_funcs = {
	.write = &stdout_writer,
};

static cookie_io_functions_t stderr_funcs = {
	.write = &stderr_writer,
};

void start_logger()
{
	int fd;

	org_stdout = stdout;
	org_stderr = stderr;

	fd = open("/dev/null", O_RDWR, 0);
	if (fd >= 0) {
		dup2(fd, STDIN_FILENO);

		if (!global_opts.foreground) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);

			openlog(BESS_ID, LOG_PID | LOG_CONS | LOG_NDELAY, 
					LOG_DAEMON);

			/* NOTE: although we replace stdout with our handler,
			 *   printf() statements that are transformed to puts()
			 *   will not be redirected to syslog,
			 *   since puts() does not use stdout, but _IO_stdout.
			 *   gcc automatically "optimizes" printf() only with
			 *   a format string that ends with '\n'.
			 *   In that case, the message will go to /dev/null
			 *   (see dup2 above). */
			stdout = fopencookie(NULL, "w", stdout_funcs);
			setvbuf(stdout, NULL, _IOLBF, 0);

			stderr = fopencookie(NULL, "w", stderr_funcs);
			setvbuf(stderr, NULL, _IOLBF, 0);
		}

		if (fd > 2)
			close(fd);
	}

	initialized = 1;
}

void end_logger()
{
	for (int i = 0; i <= MAX_LOG_PRIORITY; i++) {
		if (i < LOG_DEBUG || global_opts.debug_mode)
			log_flush(i, &loggers[i], 1);
	}

	if (!global_opts.foreground)
		closelog();

	fclose(stdout);
	stdout = org_stdout;

	fclose(stderr);
	stderr = org_stderr;

	initialized = 0;
}
