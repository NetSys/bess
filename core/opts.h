#ifndef BESS_OPTS_H_
#define BESS_OPTS_H_

#include <gflags/gflags.h>

// TODO(barath): Rename these flags to something more intuitive.
DECLARE_bool(t);
DECLARE_string(i);
DECLARE_bool(f);
DECLARE_bool(k);
DECLARE_bool(d);
DECLARE_bool(a);
DECLARE_int32(c);
DECLARE_int32(p);
DECLARE_int32(m);
DECLARE_bool(no_huge);

#endif  // BESS_OPTS_H_
