#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "error.pb.h"

typedef std::unique_ptr<bess::Error> error_ptr_t;

error_ptr_t pb_error(int code);

error_ptr_t pb_error(int code, const char *fmt, ...);

#endif
