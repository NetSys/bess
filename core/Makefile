# Copyright (c) 2014-2017, The Regents of the University of California.
# Copyright (c) 2016-2017, Nefeli Networks, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# * Neither the names of the copyright holders nor the names of their
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# Disable all implicit Makefile rules
MAKEFLAGS += --no-builtin-rules
.SUFFIXES: ;

CXX ?= g++
PROTOC ?= protoc
PKG_CONFIG ?= pkg-config

VERBOSE ?= 0

DEPDIR ?= .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$(@:.o=.d)
$(shell mkdir -p $(DEPDIR) >/dev/null)
$(shell mkdir -p $(DEPDIR)/utils >/dev/null)
$(shell mkdir -p $(DEPDIR)/modules >/dev/null)
$(shell mkdir -p $(DEPDIR)/drivers >/dev/null)
$(shell mkdir -p $(DEPDIR)/gate_hooks >/dev/null)
$(shell mkdir -p $(DEPDIR)/resume_hooks >/dev/null)
$(shell mkdir -p $(DEPDIR)/pb >/dev/null)

# 'clang' or 'g++'
CXXCOMPILER := $(shell expr $(word 1, $(shell $(CXX) --version)) : '\(clang\|g++\)')

CXXVERSION := $(shell $(CXX) -dumpversion)

ifeq "$(CXXCOMPILER)" "g++"
  ifneq "$(shell printf '$(CXXVERSION)\n7' | sort -V | head -n1)" "7"
    $(error g++ 7 or higher is required. Use container_build.py if newer g++ is not available.)
  endif
endif

HAS_PKG_CONFIG := $(shell command -v $(PKG_CONFIG) 2>&1 >/dev/null && echo yes || echo no)

# We always want these libraries to be dynamically linked even when the
# user requests a static build.
ALWAYS_DYN_LIBS := -lpthread -ldl
# These libraries are not supported by pkg-config.
ALWAYS_LIBS := -lpcap -lgflags -lnuma
# If pkg-config is available, we just need a list of the dependecies.
PKG_CONFIG_DEPS := libdpdk libglog protobuf grpc++ libunwind zlib
# If pkg-config is not available, we need to list the libs we depend on.
NO_PKG_CONFIG_LIBS := -lglog -lgflags -lprotobuf -lgrpc++ -lunwind -lz
# If pkg-config is not available and we're static linking, we also need
# the indirect dependecies.  This is annoying, because they may change
# in future versions.
NO_PKG_CONFIG_LIBS_INDIRECT := -lgrpc -lssl -lcrypto -llzma

ifeq ($(HAS_PKG_CONFIG), yes)
  PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKG_CONFIG_DEPS))
else
  PKG_CFLAGS :=
endif

# Plugins get to look in $COREDIR through -I $(COREDIR).  Let them also
# see their own top level directory, e.g., /some/path/to/modules/foo.cc
# gets /some/path/to/modules/.. so that it can read its own
# /some/path/to/utils/foo.h; and add $COREDIR/modules, so that
# relative paths from there work as if they lived in that directory.
# We give these paths to all compilations, not just plugins, but
# they are harmless for non-plugin files.
#
# TODO(torek): currently all -I are -isystem to disable warnings from
# these headers.  Should fix the warnings.  Using -isystem also disables
# -MMD dependency recording (should we use -MD?).
COREDIR := $(abspath .)
CPU ?= native
CXXFLAGS += -std=c++17 -g3 -ggdb3 -march=$(CPU) \
            -isystem $(COREDIR) \
            -isystem $(dir $<).. -isystem $(COREDIR)/modules \
            -D_GNU_SOURCE \
            -Werror -Wall -Wextra -Wcast-align -Wno-error=deprecated-declarations \
            -Wno-error=array-bounds \
            $(PKG_CFLAGS)

ifeq "$(shell test $(CXXVERSION) -ge 9; echo $$?)" "0"
  CXXFLAGS += -Wno-error=address-of-packed-member
endif

PERMISSIVE := -Wno-unused-parameter -Wno-missing-field-initializers

# -Wshadow should not be used for g++ 4.x, as it has too many false positives
ifeq "$(shell expr $(CXXCOMPILER) = g++ \& $(CXXVERSION) \< 50000)" "0"
  CXXFLAGS += -Wshadow
endif

# Disable GNU_UNIQUE symbol for g++
ifeq "$(shell expr $(CXXCOMPILER) = g++)" "1"
  CXXFLAGS += -fno-gnu-unique
endif

LDFLAGS += -rdynamic
ifdef BESS_LINK_DYNAMIC
  LIBS_ALL_SHARED = -Wl,-call_shared
  LIBS_DL_SHARED =
  ifeq ($(HAS_PKG_CONFIG), yes)
    PKG_LIBS := $(shell $(PKG_CONFIG) --libs $(PKG_CONFIG_DEPS))
  else
    PKG_LIBS := $(NO_PKG_CONFIG_LIBS)
  endif
else # Used static libraries
  LIBS_ALL_SHARED =
  LIBS_DL_SHARED = -Wl,-call_shared
  LDFLAGS += -static-libstdc++
  ifeq ($(HAS_PKG_CONFIG), yes)
    PKG_LIBS := $(shell $(PKG_CONFIG) --static --libs $(PKG_CONFIG_DEPS))
    # One of our pkg-config dependency might depend on a library that
    # we want to dynamically link no matter what.  Filter those out
    # to avoid including them into the static linking section.
    PKG_LIBS := $(filter-out $(ALWAYS_DYN_LIBS), $(PKG_LIBS))
  else
    PKG_LIBS := $(NO_PKG_CONFIG_LIBS) $(NO_PKG_CONFIG_LIBS_INDIRECT)
  endif
endif

LIBS += -Wl,-non_shared \
        $(LIBS_ALL_SHARED) \
        $(PKG_LIBS) $(ALWAYS_LIBS) \
        $(LIBS_DL_SHARED) \
        $(ALWAYS_DYN_LIBS)

ifdef SANITIZE
  CXXFLAGS += -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
  LDFLAGS += -fsanitize=address -fsanitize=undefined
endif

ifdef COVERAGE
  CXXFLAGS += --coverage -O0
  LDFLAGS += --coverage
else ifdef DEBUG
  CXXFLAGS += -O0
else
  CXXFLAGS += -O3 -DNDEBUG
endif

# Unless we explicitly specify a default goal, the first target in the
# included .mk file below will become the default goal by surprise.
.DEFAULT_GOAL := all

-include extra*.mk

# Each plugin may have their own extra Makefile snippets
-include $(addsuffix /extra*.mk,$(PLUGINS))

# Given PLUGINS (set in extra.mk), set up PROTO_PLUGINS,
# DRIVER_PLUGINS, MODULE_PLUGINS, and UTIL_PLUGINS.  Note
# that paths in extra.mk are already absolute.  Use
# $(wildcard) in case there are no drivers or modules or
# whatever: $(wildcard /path/to/nonexistent/dir) expands to
# empty, while $(wildcard /path/to/existing/dir) expands to
# /path/to/existing/dir.  This doesn't help if the PLUGINS
# path points to files (instead of directories), but that's
# a severe user-configuration error.
DRIVER_PLUGINS := $(foreach dir,$(PLUGINS),$(wildcard $(dir)/drivers))
MODULE_PLUGINS := $(foreach dir,$(PLUGINS),$(wildcard $(dir)/modules))
PROTO_PLUGINS := $(foreach dir,$(PLUGINS),$(wildcard $(dir)/protobuf))
UTIL_PLUGINS := $(foreach dir,$(PLUGINS),$(wildcard $(dir)/utils))
GATE_HOOK_PLUGINS := $(foreach dir,$(PLUGINS),$(wildcard $(dir)/gate_hooks))
RESUME_HOOK_PLUGINS := $(foreach dir,$(PLUGINS),$(wildcard $(dir)/resume_hooks))

# Much as above, ../protobuf has our standard protobufs and
# ports; there may be additional plugin ones via PLUGINS.
# Scan all these dirs and set up --proto_path argument to
# include these dirs, and add each to the vpath for %.proto
# so that Make can find the source for $< expansion.
#
# NB: protoc strips off the *first matching* leading --proto_path.
# This means we want longer paths (the ports/ sub-directories)
# to appear first.  As above, leave out the ports/ sub-directory
# if it does not exist (for plugins that do not define any
# port driver protobufs).
PROTO_DIRS := $(foreach dir,$(abspath ../protobuf) $(PROTO_PLUGINS),$(wildcard $(dir)/ports) $(wildcard $(dir)))
vpath %.proto $(PROTO_DIRS)
PROTOCFLAGS += $(PROTO_DIRS:%=--proto_path=%)
PROTOS := $(foreach dir,$(PROTO_DIRS),$(wildcard $(dir)/*.proto))
PROTO_SRCS := $(patsubst %.proto,pb/%.pb.cc, $(notdir $(PROTOS)))
PROTO_SRCS += $(patsubst %.proto,pb/%.grpc.pb.cc, $(notdir $(PROTOS)))
PROTO_HEADERS := $(patsubst %.cc,%.h, $(PROTO_SRCS))
PROTOC_CXX := $(shell which grpc_cpp_plugin)
PROTOCFLAGS += --cpp_out=./pb --grpc_out=./pb --plugin=protoc-gen-grpc=$(PROTOC_CXX)

# NB: MODULES as defined here may include test and/or benchmark
# sources -- it's just all the files in all modules/.
# Use MODULE_SRCS below, which filters them out, when that's important.
DRIVERS := $(foreach dir,drivers $(DRIVER_PLUGINS),$(wildcard $(dir)/*.cc))
MODULES := $(foreach dir,modules,$(wildcard $(dir)/*.cc))
UTILS := $(foreach dir,utils $(UTIL_PLUGINS),$(wildcard $(dir)/*.cc))
GATE_HOOK_SRCS := $(foreach dir,gate_hooks $(GATE_HOOK_PLUGINS),$(wildcard $(dir)/*.cc))
GATE_HOOKS_H := $(foreach dir,gate_hooks $(GATE_HOOK_PLUGINS),$(wildcard $(dir)/*.h))
RESUME_HOOK_SRCS := $(foreach dir,resume_hooks $(RESUME_HOOK_PLUGINS),$(wildcard $(dir)/*.cc))
RESUME_HOOKS_H := $(foreach dir,resume_hooks $(RESUME_HOOK_PLUGINS),$(wildcard $(dir)/*.h))
DRIVER_H := $(foreach dir,drivers $(DRIVER_PLUGINS),$(wildcard $(dir)/*.h))
MODULE_H := $(foreach dir,drivers,$(wildcard $(dir)/*.h))
UTIL_H := $(foreach dir,utils $(UTIL_PLUGINS),$(wildcard $(dir)/*.h))
PLUGIN_MODULES := $(foreach dir,$(MODULE_PLUGINS),$(wildcard $(dir)/*.cc))
PLUGIN_MODULE_H := $(foreach dir,$(MODULE_PLUGINS),$(wildcard $(dir)/*.h))

# NB: the first vpath is for most files, including objects for drivers,
# modules, utils, gate_hooks, and resume_hooks since those compile to
# "{drivers,modules,utils,gate_hooks,resume_hooks}/%.o".  That is,
# we just need make to search ./<path> and $(PLUGINS)/<path>.
#
# We need to keep the various names separate since, e.g., we have:
#     resume_hooks/metadata.o: resume_hooks/metadata.cc
# and yet:
#     metadata.o: metadata.cc
# (which are very different files).  We don't want to require that
# all source and object file names be unique across everything, so
# we use GNU make's "best (longest) match" feature to build the
# .o file from the appropriate source.
vpath %.cc $(PLUGINS)
# These vpaths, however, are for tests that compile to %_test.o,
# in the current directory.  There are no such tests at the moment
# for gate hooks and resume hooks, but there are some for drivers,
# modules, and utils.
vpath %.cc drivers $(DRIVER_PLUGINS)
vpath %.cc modules $(MODULE_PLUGINS)
vpath %.cc utils $(UTIL_PLUGINS)
vpath %.cc gate_hooks $(GATE_HOOK_PLUGINS)
vpath %.cc resume_hooks $(RESUME_HOOK_PLUGINS)

ALL_SRCS := $(wildcard *.cc) $(GATE_HOOKS) $(RESUME_HOOKS) $(MODULES) $(DRIVERS) $(UTILS)

TEST_SRCS := $(filter %_test.cc gtest_main.cc, $(ALL_SRCS))
TEST_OBJS := $(patsubst %.cc,%.o,$(notdir $(TEST_SRCS)))
TEST_EXEC := $(filter-out gtest_main, $(TEST_OBJS:%.o=%))
TEST_ALL_EXEC := all_test

BENCH_SRCS := $(filter %_bench.cc, $(ALL_SRCS))
BENCH_OBJS := $(patsubst %.cc,%.o,$(notdir $(BENCH_SRCS)))
BENCH_EXEC := $(BENCH_OBJS:%.o=%)

MODULE_SRCS := $(filter-out $(TEST_SRCS) $(BENCH_SRCS), $(MODULES))
MODULE_OBJS := $(addprefix modules/,$(patsubst %.cc,%.o, \
                 $(notdir $(MODULE_SRCS))))

PLUGIN_MODULE_OBJS := $(addprefix modules/,$(patsubst %.cc,%.o, \
                 $(notdir $(PLUGIN_MODULES))))
MODULE_LIBS := $(PLUGIN_MODULE_OBJS:.o=.so)

ifdef MODULE_LINK_DYNAMIC
  MODULE_LIBS += $(MODULE_OBJS:.o=.so)
endif

GATE_HOOK_OBJS := $(addprefix gate_hooks/,$(patsubst %.cc,%.o, \
                    $(notdir $(GATE_HOOK_SRCS))))
RESUME_HOOK_OBJS := $(addprefix resume_hooks/,$(patsubst %.cc,%.o, \
                      $(notdir $(RESUME_HOOK_SRCS))))

# We don't (yet?) make shared objects for drivers.
DRIVER_SRCS := $(filter-out $(TEST_SRCS) $(BENCH_SRCS), $(DRIVERS))
DRIVER_OBJS := $(addprefix drivers/,$(patsubst %.cc,%.o, \
                 $(notdir $(DRIVER_SRCS))))

UTIL_SRCS := $(filter-out $(TEST_SRCS) $(BENCH_SRCS), $(UTILS))
UTIL_OBJS := $(addprefix utils/,$(patsubst %.cc,%.o,$(notdir $(UTIL_SRCS))))

SRCS := $(filter-out $(TEST_SRCS) $(BENCH_SRCS) $(MODULE_SRCS) $(DRIVER_SRCS) $(UTIL_SRCS) $(GATE_HOOK_SRCS) $(RESUME_HOOK_SRCS), $(ALL_SRCS)) $(PROTO_SRCS)
# ? why doesn't HEADERS include module headers?
HEADERS := $(wildcard *.h utils/*.h) $(DRIVER_H) $(GATE_HOOKS_H) $(RESUME_HOOKS_H)
OBJS := $(SRCS:.cc=.o) $(DRIVER_OBJS) $(UTIL_OBJS) $(GATE_HOOK_OBJS) $(RESUME_HOOK_OBJS)

ifndef MODULE_LINK_DYNAMIC
  OBJS += $(MODULE_OBJS)
endif

EXEC := bessd

GTEST_DIR := /usr/src/gtest

.PHONY: all clean tags cscope tests benchmarks protobuf check_plugins_exist

all: $(EXEC) modules tests benchmarks check_plugins_exist

clean:
	rm -rf $(EXEC) .deps/*.d .deps/*/*.d *_test */*_test *_bench */*_bench \
		*.a pb/*.pb.* *.o */*.o *.so */*.so *.gcov *.gcda *.gcno */*.gcda */*.gcno \
		coverage.info coverage_html

tags:
	@ctags -R *

cscope:
	@rm -f cscope.*
	@find . -name "*.c" -o -name "*.h" > cscope.files
	cscope -b -q -k
	@rm -f cscope.files

tests: $(TEST_OBJS) $(TEST_EXEC) $(TEST_ALL_EXEC)

benchmarks: $(BENCH_OBJS) $(BENCH_EXEC)

protobuf: $(PROTO_SRCS)

modules: protobuf $(MODULE_OBJS) $(MODULE_LIBS)

# This just makes sure all the plugin directories actually exist
# (it's a sanity check on the value of $(PLUGINS) since if one is
# misspelled or not container-mounted, its modules just seem to
# vanish).
check_plugins_exist: $(PLUGINS)

# Generate version string from the current status of the git working copy
VERSION ?= $(shell git describe --dirty --always --tags 2>/dev/null)

# Default if previous command fails (e.g., when building from a tarball)
DEFAULT_VERSION = unknown

VERSION_LINE = \#define VERSION \"$(or $(VERSION), $(DEFAULT_VERSION))\"
version.h: $(SRCS) $(filter-out version.h, $(HEADERS))
	@echo "$(VERSION_LINE)" > $@

# Make sure version.h is created before its use.
# This is necessary since ./deps/main.d might not be there yet.
main.o: version.h

# This build wrapper takes 4 parameters:
# $(1): build type (CXX, LD, ...)
# $(2): Make target
# $(3): Make prerequisites
# $(4): command
define BUILD
$(2): $(3)
	$$(eval _TYPE := $$(strip $(1)))
	$$(eval _CMD := $$(strip $(4)))
	@if [ $$(VERBOSE) -eq 0 ]; then \
	  printf "%-11s %s\n" "[$$(_TYPE)]" "$$@"; \
	else \
	  printf "%-11s %s\n" "[$$(_TYPE)]" "$$(_CMD)"; \
	fi
	@if ! $$(_CMD); then \
	  echo "Error: \033[0;31m$$@"; \
	  echo "\033[0;33m$$(_CMD)\033[0m"; \
	  false; \
	fi
endef

$(eval $(call BUILD, \
        PROTOC, \
        pb/%.pb.cc pb/%.pb.h pb/%.grpc.pb.cc pb/%.grpc.pb.h, \
        %.proto, \
        $(PROTOC) $$< $(PROTOCFLAGS)))

$(eval $(call BUILD, \
        CXX, \
        pb/%.pb.o, \
        pb/%.pb.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $(PERMISSIVE) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        DRIVER_CXX, \
        drivers/%.o, \
        drivers/%.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        MODULE_CXX, \
        modules/%.o, \
        modules/%.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS) -fPIC))

$(eval $(call BUILD, \
        UTILS_CXX, \
        utils/%.o, \
        utils/%.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        GATEHOOK_CXX, \
        gate_hooks/%.o, \
        gate_hooks/%.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        RESUMEHOOK_CXX, \
        resume_hooks/%.o, \
        resume_hooks/%.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        MODULE_LD, \
        %.so, \
        %.o, \
        $(CXX) -shared -o $$@ $$^ $(LDFLAGS)))

$(eval $(call BUILD, \
        MODULE_TEST_LD, \
        modules/%_test, \
        modules/%_test.o modules/%.o gtest-all.o gtest_main.o bess.a, \
        $(CXX) -o $$@ $$^ $(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
        MODULE_BENCH_LD, \
        modules/%_bench, \
        modules/%_bench.o modules/%.o bess.a, \
        $(CXX) -o $$@ $$^ $(LDFLAGS) $(LIBS) -lbenchmark))

$(eval $(call BUILD, \
        CXX, \
        %.o, \
        %.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        LD, \
        $(EXEC), \
        $(OBJS), \
        $(CXX) -o $$@ $$^ $(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
        TEST_CXX, \
        %_test.o, \
        %_test.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        TEST_LD, \
        %_test, \
        %_test.o gtest-all.o gtest_main.o bess.a, \
        $(CXX) -o $$@ $$^ $(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
        TEST_LD, \
        $(TEST_ALL_EXEC), \
        $(TEST_OBJS) $(MODULE_OBJS) gtest-all.o bess.a, \
        $(CXX) -o $$@ $$^ $(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
        TEST_CXX, \
        gtest-all.o, \
        $(GTEST_DIR)/src/gtest-all.cc, \
        $(CXX) -o $$@ -c $$< -isystem $(GTEST_DIR)/include $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        BENCH_CXX, \
        %_bench.o, \
        %_bench.cc $(PROTO_HEADERS), \
        $(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
        BENCH_LD, \
        %_bench, \
        %_bench.o bess.a, \
        $(CXX) -o $$@ $$^ $(LDFLAGS) -lbenchmark $(LIBS)))

LIB_OBJS := $(filter-out main.o, $(OBJS))

$(eval $(call BUILD, \
        AR, \
        bess.a, \
        $(LIB_OBJS), \
        $(AR) rcs $$@ $$^))

.PRECIOUS: %.d $(PROTO_HEADERS)

# Note: this is slightly dangerous as it includes stale .d files
# left behind if a plug-in module is removed without doing a
# "clean".  But it's a lot easier than carefully expanding
# # out all the MODULE, DRIVER, and UTIL names to have the correct
# prefixes.
-include $(wildcard $(DEPDIR)/*.d) $(wildcard $(DEPDIR)/*/*.d)
