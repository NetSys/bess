ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)*),)
	DPDK_INC_DIR = $(RTE_SDK)/$(RTE_TARGET)/include
	DPDK_LIB_DIR = $(RTE_SDK)/$(RTE_TARGET)/lib
else
	DPDK_INC_DIR = $(RTE_SDK)/build/include
	DPDK_LIB_DIR = $(RTE_SDK)/build/lib
endif

DPDK_LIBS = -L$(DPDK_LIB_DIR)
DPDK_LIBS += -lintel_dpdk

LIBS = -ldl -lpthread -lm

CC = gcc
CFLAGS = -std=gnu99 -Wall -Werror -march=native -Wno-unused-function -Wno-unused-but-set-variable -I../sndrv -fPIC -g3 -O3 

all: libsn.so libsn.a

clean:
	rm -f *.o *.a *.so

sn.o: sn.c sn.h
	$(CC) $(CFLAGS) -I$(DPDK_INC_DIR) -c $< -o $@ 

libsn.a: sn.o
	ar rcs $@ $^

libsn.so: sn.o
	g++ -shared -o libsn.so sn.o 
