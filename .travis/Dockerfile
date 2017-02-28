FROM ubuntu:14.04

RUN apt-get -q update
# add-get-repository
RUN apt-get install -y software-properties-common
RUN apt-add-repository -y ppa:ansible/ansible
RUN apt-get -q update
# The minimum required version of g++ is 4.8
# (for gbenchmark, gcc function multiversioning, and better C++ support)
RUN apt-get install -y ansible

COPY bess.yml /tmp/bess.yml
RUN ansible-playbook /tmp/bess.yml -i "localhost," -c local --tags "package" && rm -rf /tmp/*

RUN mkdir -p /build

# Pre-build DPDK from the specified BESS branch
ARG BESS_DPDK_BRANCH
RUN cd /build && \
	git clone -b ${BESS_DPDK_BRANCH} https://github.com/netsys/bess && \
	cd /build/bess && \
	./build.py dpdk && \
	mv /build/bess/deps/dpdk-17.02 /build/dpdk-17.02 && \
	rm -rf /build/bess

WORKDIR /build/bess
