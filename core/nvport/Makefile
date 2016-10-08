CC = gcc
DPDK_LIB = dpdk

ifndef RTE_SDK
    RTE_SDK = $(abspath ../../deps/dpdk-16.07)
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

LIBS += -ldl -lpthread -lm
LIBS += -L$(DPDK_LIB_DIR) -Wl,--whole-archive -l$(DPDK_LIB) \
	-Wl,--no-whole-archive -lm -lpthread -ldl -lpcap

CC = gcc
CFLAGS += -std=gnu99 -Wall -Werror -march=native \
	  -Wno-unused-function -Wno-unused-but-set-variable \
	  -I../kmod -fPIC -g3 -O3 

all: libsn.so libsn.a

clean:
	rm -f *.o *.a *.so

sn.o: sn.c sn.h
	$(CC) $(CFLAGS) -I$(DPDK_INC_DIR) -c $< -o $@ $(LIBS) 

libsn.a: sn.o
	ar rcs $@ $^

libsn.so: sn.o
	g++ -shared -o libsn.so sn.o 
