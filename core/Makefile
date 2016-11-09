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

# e.g., 4.9.3 -> 40903
CXXVERSION := $(shell $(CXX) -dumpversion | sed -e 's/\.\([0-9][0-9]\)/\1/g' -e 's/\.\([0-9]\)/0\1/g' -e 's/^[0-9]\{3,4\}$$/&00/')

CXXVERSION_4_8_OR_HIGHER := $(shell expr $(CXXVERSION) \>= 40800)
ifeq "$(CXXVERSION_4_8_OR_HIGHER)" "0"
  $(error g++ 4.8 or higher is required)
endif

RTE_SDK ?= $(abspath ../deps/dpdk-16.07)
RTE_TARGET ?= $(shell uname -m)-native-linuxapp-gcc
DPDK_LIB ?= dpdk

ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
    DPDK_INC_DIR = $(RTE_SDK)/$(RTE_TARGET)/include
    DPDK_LIB_DIR = $(RTE_SDK)/$(RTE_TARGET)/lib
else ifneq ($(wildcard $(RTE_SDK)/build/*),)
    # if the user didn't do "make install" for DPDK
    DPDK_INC_DIR = $(RTE_SDK)/build/include
    DPDK_LIB_DIR = $(RTE_SDK)/build/lib
else
    ifeq ($(words $(MAKECMDGOALS)),1)
        ifneq ($(MAKECMDGOALS),clean)
            $(error DPDK is not available. \
                    Make sure $(abspath $(RTE_SDK)) is available and built)
        endif
    endif
endif

CXXFLAGS += -std=gnu++11 -g3 -ggdb3 -Ofast -march=native \
	    -Werror -isystem $(DPDK_INC_DIR) -D_GNU_SOURCE

HARDCORE = -Wall -Wextra -Wcast-align \
	   -Wno-missing-field-initializers	# FIXME

# -Wshadow should not be used for g++ 4.x, as it has too many false positives
CXXVERSION_5_OR_HIGHER := $(shell expr $(CXXVERSION) \>= 50000)
ifeq "$(CXXVERSION_5_OR_HIGHER)" "1"
  HARDCORE += -Wshadow
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
    CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
    LDFLAGS += -fsanitize=address -static-libasan
endif

ifdef COVERAGE
    # override -Ofast with -Og for better results
    CXXFLAGS += --coverage -Og
    LDFLAGS += --coverage
endif


-include extra.mk

PROTO_DIR = ../protobuf
PROTOS = $(wildcard $(PROTO_DIR)/*.proto)
PROTO_SRCS = $(patsubst %.proto,%.pb.cc, $(notdir $(PROTOS)))
PROTO_SRCS += $(patsubst %.proto,%.grpc.pb.cc, $(notdir $(PROTOS)))
PROTO_HEADERS = $(patsubst %.cc,%.h, $(PROTO_SRCS))
PROTOCFLAGS += --proto_path=$(PROTO_DIR) --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`

ALL_SRCS = $(wildcard *.cc utils/*.cc modules/*.cc drivers/*.cc)
HEADERS = $(wildcard *.h utils/*.h modules/*.h drivers/*.h)

TEST_SRCS = $(filter %_test.cc gtest_main.cc, $(ALL_SRCS))
TEST_OBJS = $(TEST_SRCS:.cc=.o)
TEST_EXEC = $(filter-out gtest_main, $(TEST_OBJS:%.o=%))
TEST_ALL_EXEC = all_test

BENCH_SRCS = $(filter %_bench.cc, $(ALL_SRCS))
BENCH_OBJS = $(BENCH_SRCS:.cc=.o)
BENCH_EXEC = $(BENCH_OBJS:%.o=%)

SRCS = $(filter-out $(TEST_SRCS) $(BENCH_SRCS), $(ALL_SRCS)) $(PROTO_SRCS)
OBJS = $(SRCS:.cc=.o)

EXEC = bessd

GTEST_DIR = /usr/src/gtest

.PHONY: all clean tags cscope tests benchmarks protobuf

all: $(EXEC) tests benchmarks

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

# This build wrapper takes 4 parameters:
# $(1): build type (CXX, LD, ...)
# $(2): Make target
# $(3): Make prerequisites
# $(4): command
define BUILD
$(2): $(3)
	$$(eval _TYPE = $$(strip $(1)))
	$$(eval _CMD = $$(strip $(4)))
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
	$$(PROTO_DIR)/%.proto, \
	$$(PROTOC) $$< $$(PROTOCFLAGS)))

$(eval $(call BUILD, \
	CXX, \
	%.pb.o, \
	%.pb.cc $(DEPDIR)/$$@.d, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	CXX, \
	%.o, \
	%.cc $(PROTO_HEADERS) $(DEPDIR)/$$@.d, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(HARDCORE) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	LD, \
	$$(EXEC), \
	$$(OBJS), \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $$(LIBS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	%_test.o, \
	%_test.cc $(DEPDIR)/$$@.d, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(HARDCORE) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	TEST_LD, \
	%_test, \
	%_test.o gtest-all.o gtest_main.o bess.a, \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
	TEST_LD, \
	$$(TEST_ALL_EXEC), \
	$$(TEST_OBJS) gtest-all.o bess.a, \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	gtest-all.o, \
	$$(GTEST_DIR)/src/gtest-all.cc, \
	$$(CXX) -o $$@ -c $$< -I$$(GTEST_DIR) $$(CXXFLAGS) $$(HARDCORE) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	BENCH_CXX, \
	%_bench.o, \
	%_bench.cc $(DEPDIR)/$$@.d, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS) $$(HARDCORE) $$(DEPFLAGS)))

$(eval $(call BUILD, \
	BENCH_LD, \
	%_bench, \
	%_bench.o %.o bess.a, \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) -lbenchmark $(LIBS)))

CORE_OBJS = $(filter-out main.o %_bench.o, $(OBJS))

$(eval $(call BUILD, \
	AR, \
	bess.a, \
	$(CORE_OBJS), \
	$$(AR) rcs $$@ $$^))

%.d: ;

.PRECIOUS: %.d $(PROTO_HEADERS)

-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(ALL_SRCS)))
