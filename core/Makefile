# Disable all implicit Makefile rules
MAKEFLAGS += --no-builtin-rules
.SUFFIXES: ;

CXX ?= g++
PROTOC ?= protoc

VERBOSE ?= 0

DEPDIR ?= .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$(@:.o=.d)
$(shell mkdir -p $(DEPDIR) >/dev/null)
$(shell mkdir -p $(DEPDIR)/utils >/dev/null)
$(shell mkdir -p $(DEPDIR)/modules >/dev/null)
$(shell mkdir -p $(DEPDIR)/drivers >/dev/null)
$(shell mkdir -p $(DEPDIR)/hooks >/dev/null)

# 'clang' or 'g++'
CXXCOMPILER := $(shell expr $(word 1, $(shell $(CXX) --version)) : '\(clang\|g++\)')

# e.g., 4.9.3 -> 40903. For clang it is always 40201
CXXVERSION := $(shell $(CXX) -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/')

ifeq "$(shell expr $(CXXCOMPILER) = g++ \& $(CXXVERSION) \< 40800)" "1"
    $(error g++ 4.8 or higher is required)
endif

RTE_SDK ?= $(abspath ../deps/dpdk-17.02)
RTE_TARGET ?= $(shell uname -m)-native-linuxapp-gcc
DPDK_LIB ?= dpdk

ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
	DPDK_INC_DIR := $(RTE_SDK)/$(RTE_TARGET)/include
	DPDK_LIB_DIR := $(RTE_SDK)/$(RTE_TARGET)/lib
else ifneq ($(wildcard $(RTE_SDK)/build/*),)
	# if the user didn't do "make install" for DPDK
	DPDK_INC_DIR := $(RTE_SDK)/build/include
	DPDK_LIB_DIR := $(RTE_SDK)/build/lib
else ifeq ($(words $(MAKECMDGOALS)),1)
	ifneq ($(MAKECMDGOALS),clean)
	$(error DPDK is not available. \
		Make sure $(abspath $(RTE_SDK)) is available and built)
	endif
endif

CXXFLAGS += -std=gnu++11 -g3 -ggdb3 -march=native \
	    -Werror -isystem $(DPDK_INC_DIR) -isystem . -D_GNU_SOURCE \
	    -Wall -Wextra -Wcast-align

PERMISSIVE := -Wno-unused-parameter -Wno-missing-field-initializers \
	      -Wno-unused-private-field

# -Wshadow should not be used for g++ 4.x, as it has too many false positives
ifeq "$(shell expr $(CXXCOMPILER) = g++ \& $(CXXVERSION) \< 50000)" "0"
	CXXFLAGS += -Wshadow
endif

LDFLAGS += -rdynamic -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR) -pthread \
	   -static-libstdc++

LIBS += -Wl,-non_shared \
	-Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive \
	-lglog -lgflags -lprotobuf -lgrpc++ -lgrpc \
	-lssl -lcrypto -lunwind -llzma -lpcap -lz \
	-Wl,-call_shared \
	-ldl

ifdef SANITIZE
	CXXFLAGS += -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
	LDFLAGS += -fsanitize=address -fsanitize=undefined
endif

ifdef DEBUG
	CXXFLAGS += --coverage -O1
	LDFLAGS += --coverage
else
	CXXFLAGS += -Ofast -DNDEBUG
endif

-include extra.dpdk.mk
-include extra.mk

PROTO_DIR := $(abspath ../protobuf)
PROTOS := $(wildcard $(PROTO_DIR)/*.proto)
PROTO_SRCS := $(patsubst %.proto,%.pb.cc, $(notdir $(PROTOS)))
PROTO_SRCS += $(patsubst %.proto,%.grpc.pb.cc, $(notdir $(PROTOS)))
PROTO_HEADERS := $(patsubst %.cc,%.h, $(PROTO_SRCS))
PROTOCFLAGS += --proto_path=$(PROTO_DIR) --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`

ALL_SRCS := $(wildcard *.cc utils/*.cc modules/*.cc drivers/*.cc hooks/*.cc)
HEADERS := $(wildcard *.h utils/*.h modules/*.h drivers/*.h hooks/*.h)

TEST_SRCS := $(filter %_test.cc gtest_main.cc, $(ALL_SRCS))
TEST_OBJS := $(TEST_SRCS:.cc=.o)
TEST_EXEC := $(filter-out gtest_main, $(TEST_OBJS:%.o=%))
TEST_ALL_EXEC := all_test

BENCH_SRCS := $(filter %_bench.cc, $(ALL_SRCS))
BENCH_OBJS := $(BENCH_SRCS:.cc=.o)
BENCH_EXEC := $(BENCH_OBJS:%.o=%)

MODULE_SRCS := $(filter-out $(TEST_SRCS) $(BENCH_SRCS), $(filter modules/%.cc, $(ALL_SRCS)))
MODULE_OBJS := $(MODULE_SRCS:.cc=.o)
MODULE_LIBS := $(MODULE_OBJS:.o=.so)

SRCS := $(filter-out $(TEST_SRCS) $(BENCH_SRCS) $(MODULE_SRCS), $(ALL_SRCS)) $(PROTO_SRCS)
OBJS := $(SRCS:.cc=.o)

EXEC := bessd

GTEST_DIR := /usr/src/gtest

.PHONY: all clean tags cscope tests benchmarks protobuf

all: $(EXEC) modules tests benchmarks

clean:
	rm -rf $(EXEC) .deps/*.d .deps/*/*.d *_test */*_test *_bench */*_bench \
		*.a *.pb.* *.o */*.o *.gcda *.gcno */*.gcda */*.gcno \
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
	%.pb.cc %.pb.h %.grpc.pb.cc %.grpc.pb.h, \
	$(PROTO_DIR)/%.proto, \
	$(PROTOC) $$< $(PROTOCFLAGS)))

$(eval $(call BUILD, \
	CXX, \
	%.pb.o, \
	%.pb.cc, \
	$(CXX) -o $$@ -c $$< $(CXXFLAGS) $(PERMISSIVE) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	MODULE_CXX, \
	modules/%.o, \
	modules/%.cc, \
	$(CXX) -o $$@ -c $$< $(CXXFLAGS) $$(DEPFLAGS) -fPIC))

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
	$(CXX) -o $$@ -c $$< $(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	LD, \
	$(EXEC), \
	$(OBJS), \
	$(CXX) -o $$@ $$^ $(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	%_test.o, \
	%_test.cc, \
	$(CXX) -o $$@ -c $$< $(CXXFLAGS) $$(DEPFLAGS)))

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
	$(CXX) -o $$@ -c $$< -isystem $(GTEST_DIR) $(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	BENCH_CXX, \
	%_bench.o, \
	%_bench.cc, \
	$(CXX) -o $$@ -c $$< $(CXXFLAGS) $$(DEPFLAGS)))

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

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(ALL_SRCS)))
