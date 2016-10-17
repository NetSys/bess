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

CFLAGS += -std=gnu++11 -g3 -ggdb3 -Ofast -march=native \
	  -Wall -Werror -Wno-unused-function -Wno-unused-but-set-variable \
	  -I$(DPDK_INC_DIR) \
	  -fno-rtti \
	  -D_GNU_SOURCE

LDFLAGS += -rdynamic -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR)
LIBS += -Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive -lm -pthread -lgflags -ldl -lrt -lpcap

ifdef SANITIZE
    LDFLAGS += -fsanitize=address
    CFLAGS += -fsanitize=address -fno-omit-frame-pointer
endif

-include extra.mk

SRCS = $(wildcard *.c utils/*.c tests/*.c modules/*.cc drivers/*.cc)
OBJS = $(patsubst %.c,%.o, $(filter %.c, $(SRCS))) $(patsubst %.cc,%.o, $(filter %.cc, $(SRCS)))
HEADERS = $(wildcard *.h utils/*.h tests/*.h modules/*.h drivers/*.h)
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

.PHONY: all clean tags cscope

all: $(DEPS) $(EXEC)

$(DEPS): $(SRCS) $(HEADERS)
	@$(CXX) $(CFLAGS) -MM $(SRCS) | sed 's|\(.*\)\.o: \(.*\)\.c|\2.o: \2.c|' > $(DEPS)

%.o: %.c
	$(eval COMMAND = $(CXX) $(CFLAGS) -c -o $@ $<)
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

%.o: %.cc
	$(eval COMMAND = $(CXX) $(CFLAGS) -c -o $@ $<)
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
	rm -f $(DEPS) $(EXEC) *.o modules/*.o drivers/*.o

tags:
	@ctags -R *

cscope:
	@rm -f cscope.*
	@find . -name "*.c" -o -name "*.h" > cscope.files
	cscope -b -q -k
	@rm -f cscope.files

endif
