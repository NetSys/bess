# BESS must be already built.
# Set your BESS_HOME environment variable to that directory.
#BESS_HOME ?= /your/bess/home

ifeq ($(BESS_HOME),)
$(error BESS_HOME is not defined.)
endif

# Library target
target_a := modules/sequential_update.so
# Objects for the target
objects_a := modules/sequential_update.o

# Add all your targets here
all: $(target_a) # $(target_b) ...

# You can specify per-target flags like this
#$(target): LDFLAGS+=

# Protobuf file location in 'absolute path'
protobuf := $(abspath protobuf/supdate_msg.proto)

#### You don't need to modify from here ####
proto_h := $(patsubst %.proto,%.pb.h, $(protobuf)) \
	   $(patsubst %.proto,%.grpc.pb.h, $(protobuf))
proto_cc := $(patsubst %.proto,%.pb.cc, $(protobuf)) \
	    $(patsubst %.proto,%.grpc.pb.cc, $(protobuf))
proto_o := $(patsubst %.cc,%.o, $(proto_cc))
proto_py := $(patsubst %.proto, %_pb2.py, $(protobuf)) \
	    $(patsubst %.proto, %_pb2_grpc.py, $(protobuf))

$(proto_cc): $(protobuf) $(proto_py)
$(proto_cc): PROTO_DIR=$(dir $(protobuf))
$(proto_py): $(protobuf)
$(proto_py): PROTO_DIR=$(dir $(protobuf))
####             up to here            ####

# Mapping between target library, protobuf, objects
# protobuf objects should appear before objects
$(target_a): $(proto_o) $(objects_a)
# $(target_b): $(proto_o) $(objects_b)

clean:
	rm -f $(proto_o) $(proto_cc) $(proto_h) $(proto_py) \
		$(target_a) $(objects_a)

include $(BESS_HOME)/core/bess_plugin.mk
