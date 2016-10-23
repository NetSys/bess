# Disable all implicit Makefile rules
.SUFFIXES:

CXX ?= g++
PROTOC ?= protoc

RTE_SDK ?= $(abspath ../deps/dpdk-16.07)
RTE_TARGET ?= $(shell uname -m)-native-linuxapp-gcc
DPDK_LIB = dpdk

ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
    DPDK_INC_DIR = $(RTE_SDK)/$(RTE_TARGET)/include
    DPDK_LIB_DIR = $(RTE_SDK)/$(RTE_TARGET)/lib
else ifneq ($(wildcard $(RTE_SDK)/build/*),)
    # if the user didn't do "make install" for DPDK
    DPDK_INC_DIR = $(RTE_SDK)/build/include
    DPDK_LIB_DIR = $(RTE_SDK)/build/lib
else
    $(error DPDK is not available. \
            Make sure $(abspath $(RTE_SDK)) is available and built)
endif

CXXFLAGS += -std=gnu++11 -g3 -ggdb3 -Ofast -march=native \
	    -Wall -Werror -Wno-unused-function -Wno-unused-but-set-variable \
	    -I$(DPDK_INC_DIR) \
	    -D_GNU_SOURCE

LDFLAGS += -rdynamic -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR) -pthread
LIBS += -Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive \
	-lm -lgflags -lglog -ldl -lrt -lpcap -lprotobuf -lgrpc++ -lgrpc++_reflection

ifdef SANITIZE
    LDFLAGS += -fsanitize=address
    CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif

-include extra.mk

PROTO_DIR = ../protobuf
PROTOS = $(wildcard $(PROTO_DIR)/*.proto)
PROTO_SRCS = $(patsubst %.proto,%.pb.cc, $(notdir $(PROTOS)))
PROTO_SRCS += $(patsubst %.proto,%.grpc.pb.cc, $(notdir $(PROTOS)))
PROTO_HEADERS = $(patsubst %.cc,%.h, $(PROTO_SRCS))
PROTOCFLAGS += --proto_path=$(PROTO_DIR) --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin`

ALL_SRCS = $(wildcard *.cc utils/*.cc modules/*.cc drivers/*.cc oldtests/*.cc)
HEADERS = $(wildcard *.h utils/*.h modules/*.h drivers/*.h oldtests/*.h)

TEST_SRCS = $(filter %_test.cc, $(ALL_SRCS))
TEST_OBJS = $(TEST_SRCS:.cc=.o)
TEST_EXEC = $(TEST_OBJS:%.o=%)

SRCS = $(filter-out $(TEST_SRCS), $(ALL_SRCS))
OBJS = $(SRCS:.cc=.o)

EXEC = bessd
DEPS = .make.dep

GTEST_DIR = /usr/src/gtest

# if multiple targets are specified, do them one by one */
ifneq ($(words $(MAKECMDGOALS)),1)

.NOTPARALLEL:
$(sort all $(MAKECMDGOALS)):
	@$(MAKE) --no-print-directory -f $(firstword $(MAKEFILE_LIST)) $@

else

# parallel build by default
CORES ?= $(shell nproc || echo 1)
MAKEFLAGS += -j $(CORES)
VERBOSE ?= 0

.PHONY: all clean tags cscope tests protobuf

all: $(DEPS) $(EXEC)

clean:
	rm -f $(DEPS) $(EXEC) $(TEST_EXEC) *.a *.pb.* *.o modules/*.o drivers/*.o utils/*.o oldtests/*.o

tags:
	@ctags -R *

cscope:
	@rm -f cscope.*
	@find . -name "*.c" -o -name "*.h" > cscope.files
	cscope -b -q -k
	@rm -f cscope.files

tests: $(DEPS) $(TEST_OBJS) $(TEST_EXEC)

protobuf: $(PROTO_SRCS)

# This build wrapper takes 4 parameters:
# $(1): build type (CXX, LD, ...)
# $(2): Make target
# $(3): Make prerequisites
# $(4): command
define BUILD =
$(2): $(3)
	$$(eval _TYPE = $$(strip $(1)))
	$$(eval _TARGET = $$(if $$(word 2, $$^), $$@, $$<))
	$$(eval _CMD = $$(strip $(4)))
	@if [ $$(VERBOSE) -eq 0 ]; then \
		printf "%-10s %s\n" [$$(_TYPE)] $$(_TARGET); \
	else \
		printf "%-10s %s\n" [$$(_TYPE)] $$(_CMD); \
	fi
	@if ! $$(_CMD); then \
		echo "Error: \033[0;31m$$(_TARGET)"; \
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
	DEPS, \
	$$(DEPS), \
	$$(PROTO_HEADERS) $$(SRCS) $$(HEADERS), \
	$$(CXX) $$(CXXFLAGS) -MM $$(SRCS) | sed 's|\(.*\)\.o: \(.*\)\.c|\2.o: \2.c|' > $$(DEPS)))

$(eval $(call BUILD, \
	CXX, \
	%.o, \
	%.cc, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS)))

$(eval $(call BUILD, \
	LD, \
	$$(EXEC), \
	$$(OBJS), \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $$(LIBS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	%_test.o, \
	%_test.cc, \
	$$(CXX) -o $$@ -c $$< $$(CXXFLAGS)))

$(eval $(call BUILD, \
	TEST_LD, \
	%_test, \
	%_test.o %.o gtest_main.a bess.a, \
	$$(CXX) -o $$@ $$^ $$(LDFLAGS) $(LIBS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	gtest-all.o, \
	$$(GTEST_DIR)/src/gtest-all.cc, \
	$$(CXX) -o $$@ -c $$< -I$$(GTEST_DIR) $$(CXXFLAGS)))

$(eval $(call BUILD, \
	TEST_CXX, \
	gtest_main.o, \
	$$(GTEST_DIR)/src/gtest_main.cc, \
	$$(CXX) -o $$@ -c $$< -I$$(GTEST_DIR) $$(CXXFLAGS)))

$(eval $(call BUILD, \
	TEST_AR, \
	gtest_main.a, \
	gtest-all.o gtest_main.o, \
	$$(AR) rcs $$@ $$^))

CORE_OBJS = $(filter-out main.o drivers/%.o modules/%.o oldtests/%.o, $(OBJS))
$(eval $(call BUILD, \
	TEST_AR, \
	bess.a, \
	$(CORE_OBJS), \
	$$(AR) rcs $$@ $$^))

-include $(DEPS)

endif
