ifndef CXX
    CXX = g++
endif

DPDK_LIB = dpdk

ifndef RTE_SDK
    RTE_SDK = $(abspath ../deps/dpdk-16.07)
endif

ifndef RTE_TARGET
    RTE_TARGET = $(shell uname -m)-native-linuxapp-gcc
endif

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

# gtest definitions
GTEST_DIR = /usr/src/gtest
CXXFLAGS += -isystem $(GTEST_DIR)/include
CXXFLAGS += -g -Wall -Wextra -pthread

CXXFLAGS += -std=gnu++11 -g3 -ggdb3 -Ofast -march=native \
	    -Wall -Werror -Wno-unused-function -Wno-unused-but-set-variable \
	    -I$(DPDK_INC_DIR) \
	    -fno-rtti \
	    -D_GNU_SOURCE

LDFLAGS += -rdynamic -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR)
LIBS += -Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive -lm -pthread -lgflags -lglog -ldl -lrt -lpcap

ifdef SANITIZE
    LDFLAGS += -fsanitize=address
    CXXFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif

-include extra.mk

SRCS = $(wildcard *.cc utils/*.cc modules/*.cc drivers/*.cc oldtests/*.cc)
OBJS = $(patsubst %.cc,%.o, $(filter %.cc, $(SRCS)))
HEADERS = $(wildcard *.h utils/*.h modules/*.h drivers/*.h oldtests/*.h)
EXEC = bessd

DEPS = .make.dep

# if multiple targets are specified, do them one by one */
ifneq ($(words $(MAKECMDGOALS)),1)

.NOTPARALLEL:
$(sort all $(MAKECMDGOALS)):
	@$(MAKE) --no-print-directory -f $(firstword $(MAKEFILE_LIST)) $@

else

# parallel build by default
CORES ?= $(shell nproc || echo 1)
MAKEFLAGS += -j $(CORES)
VERBOSE = 0

.PHONY: all clean tests tags cscope

all: $(DEPS) $(EXEC)

$(DEPS): $(SRCS) $(HEADERS)
	@$(CXX) $(CXXFLAGS) -MM $(SRCS) | sed 's|\(.*\)\.o: \(.*\)\.c|\2.o: \2.c|' > $(DEPS)

%.o: %.cc
	$(eval COMMAND = $(CXX) $(CXXFLAGS) -c -o $@ $<)
	@if $(COMMAND); then \
		if [ $(VERBOSE) -eq 0 ]; then \
			echo "[CXX] $<"; \
		else \
			echo "[CXX] $(COMMAND)"; \
		fi \
	else \
		echo "Compile error: \033[0;31m$<\033[0;33m\n$(COMMAND)\033[0m"; \
		false; \
	fi

$(EXEC): $(OBJS)
	$(eval COMMAND = $(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LIBS))
	@if $(COMMAND); then \
		if [ $(VERBOSE) -eq 0 ]; then \
			echo "[LD] $@"; \
		else \
			echo "[LD] $(COMMAND)"; \
		fi \
	else \
		echo "Link error: \033[0;31m$@\033[0;33m\n$(COMMAND) \033[0m"; \
		false; \
	fi
	@echo "Build succeeded."
	@sync

-include $(DEPS)

clean:
	rm -f $(DEPS) $(EXEC) $(TESTS) *.o modules/*.o drivers/*.o utils/*.o oldtests/*.o gtest.a gtest_main.a

tags:
	@ctags -R *

cscope:
	@rm -f cscope.*
	@find . -name "*.c" -o -name "*.h" > cscope.files
	cscope -b -q -k
	@rm -f cscope.files

# Test rules
TESTS = time_test

tests: $(TESTS)

# Rules to build gtest library.
GTEST_SRCS_ = $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)

# For simplicity and to avoid depending on Google Test's
# implementation details, the dependencies specified below are
# conservative and not optimized.  This is fine as Google Test
# compiles fast and for ordinary users its source rarely changes.
gtest-all.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c \
            $(GTEST_DIR)/src/gtest-all.cc

gtest_main.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) $(CXXFLAGS) -c \
            $(GTEST_DIR)/src/gtest_main.cc

gtest.a : gtest-all.o
	$(AR) $(ARFLAGS) $@ $^

gtest_main.a : gtest-all.o gtest_main.o
	$(AR) $(ARFLAGS) $@ $^

# Time test build rules.
time_test.o : time_test.cc time.h $(GTEST_HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c time_test.cc

time_test : time.o time_test.o gtest_main.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -lpthread $^ -o $@

endif
