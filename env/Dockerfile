# vim: syntax=dockerfile

# Bionic Beaver (18.04) does not require ppa repositories for any packages
# we need, such as g++-7, clang-6.0, ansible, grpc, etc.
ARG BASE_IMAGE=ubuntu:bionic
FROM ${BASE_IMAGE}

# Install Ansible
RUN apt-get -q update
RUN apt-get install -y software-properties-common
RUN apt-add-repository -y ppa:ansible/ansible
RUN apt-get -q update
RUN apt-get install -y ansible git

COPY build-dep.yml /tmp/
COPY kmod.yml /tmp/
COPY ci.yml /tmp/
RUN ANSIBLE_STDOUT_CALLBACK=debug ansible-playbook /tmp/ci.yml -i "localhost," -c local && rm -rf /tmp/*

RUN mkdir -p /build

# Pre-build DPDK from the specified BESS branch
ARG BESS_DPDK_BRANCH=master
RUN cd /build && \
	git clone -b ${BESS_DPDK_BRANCH} https://github.com/netsys/bess && \
	cd /build/bess && \
	./build.py dpdk && \
	mv /build/bess/deps/dpdk-17.11 /build/dpdk-17.11 && \
	rm -rf /build/bess

ENV CCACHE_DIR=/tmp/ccache
ENV CCACHE_COMPRESS=true

WORKDIR /build/bess
