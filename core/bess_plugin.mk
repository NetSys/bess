# Copyright (c) 2017, The Regents of the University of California.
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

CXX ?= g++
PROTOC ?= protoc

# 'clang' or 'g++'
CXXCOMPILER := $(shell expr $(word 1, $(shell $(CXX) --version)) : '\(clang\|g++\)')

CXXVERSION := $(shell $(CXX) -dumpversion)


ifeq "$(CXXCOMPILER)" "g++"
ifneq "$(shell printf '$(CXXVERSION)\n5' | sort -V | head -n1)" "5"
$(error g++ 5 or higher is required. Use container_build.py if newer g++ is not available.)
endif
endif

RTE_SDK ?= $(BESS_HOME)/deps/dpdk-17.05
RTE_TARGET ?= $(shell uname -m)-native-linuxapp-gcc
DPDK_LIB ?= dpdk

ifneq ($(wildcard $(RTE_SDK)/$(RTE_TARGET)/*),)
	DPDK_INC_DIR := $(RTE_SDK)/$(RTE_TARGET)/include
else ifneq ($(wildcard $(RTE_SDK)/build/*),)
	# if the user didn't do "make install" for DPDK
	DPDK_INC_DIR := $(RTE_SDK)/build/include
else ifeq ($(words $(MAKECMDGOALS)),1)
	ifneq ($(MAKECMDGOALS),clean)
	$(error DPDK is not available. \
		Make sure $(abspath $(RTE_SDK)) is available and built.)
	endif
endif

BESS_INC_DIR := $(BESS_HOME)/core

CXXARCHFLAGS ?= -march=native
CXXFLAGS += -std=c++11 -g3 -ggdb3 $(CXXARCHFLAGS) \
	    -Werror -isystem $(DPDK_INC_DIR) -isystem . -D_GNU_SOURCE \
	    -isystem $(BESS_INC_DIR) \
	    -Wall -Wextra -Wcast-align

PERMISSIVE := -Wno-unused-parameter -Wno-missing-field-initializers \
	      -Wno-unused-private-field

# -Wshadow should not be used for g++ 4.x, as it has too many false positives
ifeq "$(shell expr $(CXXCOMPILER) = g++ \& $(CXXVERSION) \< 50000)" "0"
	CXXFLAGS += -Wshadow
endif

# Disable GNU_UNIQUE symbol for g++
ifeq "$(shell expr $(CXXCOMPILER) = g++)" "1"
	CXXFLAGS += -fno-gnu-unique
endif

ifdef SANITIZE
	CXXFLAGS += -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer
	LDFLAGS += -fsanitize=address -fsanitize=undefined
endif

ifdef DEBUG
	CXXFLAGS += -O0
else
	CXXFLAGS += -Ofast -DNDEBUG
endif

PROTOCFLAGS += --proto_path=$(PROTO_DIR) \
	       --cpp_out=$(PROTO_DIR) --grpc_out=$(PROTO_DIR) \
	       --plugin=protoc-gen-grpc=$(shell which grpc_cpp_plugin)

PROTOPYFLAGS += --proto_path=$(PROTO_DIR) \
	        --python_out=$(PROTO_DIR) --grpc_out=$(PROTO_DIR) \
	        --plugin=protoc-gen-grpc=$(shell which grpc_python_plugin)

%.pb.o: %.pb.cc
	$(CXX) -o $@ -c $< $(CXXFLAGS) $(PERMISSIVE) -fPIC

%.pb.cc:
	$(PROTOC) $< $(PROTOCFLAGS)

%_pb2.py:
	protoc $< $(PROTOPYFLAGS)
	mv $(PROTO_DIR)/*_pb2.py $(BESS_HOME)/pybess/plugin_pb/

%.o: %.cc
	$(CXX) -o $@ -c $^ $(CXXFLAGS) $(MODULE_CXXFLAGS) -fPIC

%.so:
	$(CXX) -shared -o $@ $^ $(LDFLAGS) $(MODULE_LDFLAGS)
