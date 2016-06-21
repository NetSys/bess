#ifndef _LOG_H_
#define _LOG_H_

#include <syslog.h>

/* the maximum log size is 2KB, including the trailing NULL character */
#define MAX_LOG_LEN	2048

void log_flush(int priority);

/* Corresponds to perror(). No LF should be given at the end */
void log_perr(const char *fmt, ...)
		__attribute__((format(printf, 1, 2)));

void _log(int priority, const char *fmt, ...) 
		__attribute__((format(printf, 2, 3)));

void start_logger();
void end_logger();

/* do not use the following two */ 
#define log_emerg(fmt, ...)	_log(LOG_EMERG, fmt, ##__VA_ARGS__)
#define log_alert(fmt, ...)	_log(LOG_ALERT, fmt, ##__VA_ARGS__)

#define log_crit(fmt, ...) 	_log(LOG_CRIT, fmt, ##__VA_ARGS__)
#define log_err(fmt, ...)  	_log(LOG_ERR, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) 	_log(LOG_WARNING, fmt, ##__VA_ARGS__)
#define log_notice(fmt, ...)	_log(LOG_NOTICE, fmt, ##__VA_ARGS__)
#define log_info(fmt, ...) 	_log(LOG_INFO, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...)	_log(LOG_DEBUG, fmt, ##__VA_ARGS__)

#endif
