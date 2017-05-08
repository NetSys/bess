#include "message.h"

#include <string>

#include "utils/format.h"

pb_error_t pb_error(int code, const char *fmt, ...) {
  pb_error_t p;

  p.set_code(code);

  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    const std::string message = bess::utils::FormatVarg(fmt, ap);
    va_end(ap);
    p.set_errmsg(message);
  }

  return p;
}

using CommandError = bess::pb::Error;

CommandResponse CommandSuccess() {
  return CommandResponse();
}

CommandResponse CommandSuccess(const google::protobuf::Message &return_data) {
  CommandResponse ret;
  ret.mutable_data()->PackFrom(return_data);
  return ret;
}

CommandResponse CommandFailure(int code) {
  CommandResponse ret;
  CommandError *error = ret.mutable_error();
  error->set_code(code);
  error->set_errmsg(strerror(code));
  return ret;
}

CommandResponse CommandFailure(int code, const char *fmt, ...) {
  CommandResponse ret;
  CommandError *error = ret.mutable_error();

  error->set_code(code);

  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    const std::string message = bess::utils::FormatVarg(fmt, ap);
    va_end(ap);
    error->set_errmsg(message);
  }

  return ret;
}
