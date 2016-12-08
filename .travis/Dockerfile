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
		clang++-3.8 clang-3.8 \
		autoconf libtool make cmake \
		libpthread-stubs0-dev libunwind8-dev \
		zlib1g-dev liblzma-dev libpcap-dev libssl-dev \
		curl git python2.7 python \
		libgtest-dev libgflags-dev libgoogle-glog-dev \
		linux-headers-generic.* && \
	apt-get clean

RUN mkdir -p /build

# There is no package that has grpc_cpp_plugin in Ubuntu (true?),
# so build it from source
RUN cd /build && \
	git clone -b $(curl -s -L http://grpc.io/release) https://github.com/grpc/grpc && \
	cd /build/grpc && \
	git submodule update --init && \
	make -j && \
	make install && \
	cd /build/grpc/third_party/protobuf && \
	make -j && \
	make install && \
	rm -rf /build/grpc

# Same for Google benchmark
RUN cd /build && \
	git clone https://github.com/google/benchmark && \
	cd /build/benchmark && \
	git checkout 4f8bfea && \
	mkdir -p /build/benchmark/build && \
	cd /build/benchmark/build && \
	cmake .. && \
	make && \
	make install && \
	rm -rf /build/benchmark

# Pre-build DPDK from the specified BESS branch
ARG BESS_DPDK_BRANCH
RUN cd /build && \
	git clone -b ${BESS_DPDK_BRANCH} https://github.com/netsys/bess && \
	cd /build/bess && \
	./build.py dpdk && \
	mv /build/bess/deps/dpdk-16.07 /build/dpdk-16.07 && \
	rm -rf /build/bess

WORKDIR /build/bess
