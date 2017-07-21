BESS_HOME ?= /home/shinae/bess

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
proto_h :=  $(patsubst %.proto,%.pb.h, $(protobuf)) \
						$(patsubst %.proto,%.grpc.pb.h, $(protobuf))
proto_cc :=  $(patsubst %.proto,%.pb.cc, $(protobuf)) \
						$(patsubst %.proto,%.grpc.pb.cc, $(protobuf))
proto_o := $(patsubst %.cc,%.o, $(proto_cc))

$(proto_cc): PROTO_DIR=$(dir $(protobuf))
$(proto_cc): $(protobuf)
####             up to here            ####

# Mapping between target library, protobuf, objects
# protobuf objects should appear before objects
$(target_a): $(proto_o) $(objects_a)
# $(target_b): $(proto_o) $(objects_b)

clean:
	rm -f $(proto_o) $(proto_cc) $(proto_h) \ 
		$(target_a) $(objects_a)

include bess_plugin.mk
