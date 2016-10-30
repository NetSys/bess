FROM ubuntu:14.04

RUN apt-get -q update
# add-get-repository
RUN apt-get install -y software-properties-common
# for the latest version of g++
RUN add-apt-repository -y ppa:ubuntu-toolchain-r/test
RUN apt-get -q update
# The minimum required version of g++ is 4.8
# (for gbenchmark, gcc function multiversioning, and better C++ support)
RUN apt-get install -y g++-4.8 g++-5 g++-6 g++ \
	autoconf libtool make cmake \
	libpthread-stubs0-dev libunwind8-dev \
	zlib1g-dev liblzma-dev libpcap-dev libssl-dev \
	curl git python2.7 python \
	libgtest-dev libgflags-dev libgoogle-glog-dev
RUN apt-get install -y linux-headers-generic.*

# There is no package that has grpc_cpp_plugin in Ubuntu (true?),
# so build it from source
RUN mkdir -p /build && cd /build && git clone -b $(curl -s -L http://grpc.io/release) https://github.com/grpc/grpc
RUN cd /build/grpc && git submodule update --init && make -j && make install
RUN cd /build/grpc/third_party/protobuf && make -j && make install

# Same for Google benchmark
RUN cd /build && git clone -b v1.0.0 https://github.com/google/benchmark && mkdir -p benchmark/build
RUN cd /build/benchmark/build && cmake .. && make && make install

WORKDIR /build/bess
