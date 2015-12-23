CC = gcc
DPDK_LIB = dpdk

ifndef RTE_SDK
    RTE_SDK = $(abspath ../deps/dpdk-2.2.0)
endif

ifndef RTE_TARGET
    RTE_TARGET = x86_64-native-linuxapp-gcc
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

KMOD_INC_DIR = kmod

LDFLAGS += -rdynamic -L$(DPDK_LIB_DIR) -Wl,-rpath=$(DPDK_LIB_DIR)
LIBS += -Wl,--whole-archive -l$(DPDK_LIB) -Wl,--no-whole-archive -lm -lpthread -ldl -lpcap

CFLAGS += -std=gnu99 -g3 -ggdb3 -Ofast -Wall -Werror -m64 -march=native \
	  -Wno-unused-function -Wno-unused-but-set-variable \
	  -I $(KMOD_INC_DIR) -I$(DPDK_INC_DIR) \
	  -D_GNU_SOURCE

-include extra.mk

SRCS = $(wildcard *.c modules/*.c drivers/*.c)
OBJS = $(SRCS:.c=.o)
HEADERS = $(wildcard *.h modules/*.h drivers/*.h)
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

.PHONY: all clean tags cscope

all: $(DEPS) $(EXEC)
	
$(DEPS): $(SRCS) $(HEADERS)
	@$(CC) $(CFLAGS) -MM $(SRCS) | sed 's|\(.*\)\.o: \(.*\)\.c|\2.o: \2.c|' > $(DEPS);

$(EXEC): $(OBJS)
	@echo "Using DPDK from $(DPDK_LIB_DIR)"
	$(CC) $(OBJS) -o $@ $(LDFLAGS) $(LIBS)
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
