# vim: syntax=dockerfile

ARG BASE_IMAGE=ubuntu:focal
FROM ${BASE_IMAGE}

RUN echo "APT::Install-Recommends false;" >> /etc/apt/apt.conf.d/00recommends && \
	echo "APT::Install-Suggests false;" >> /etc/apt/apt.conf.d/00recommends && \
	echo "APT::AutoRemove::RecommendsImportant false;" >> /etc/apt/apt.conf.d/00recommends && \
	echo "APT::AutoRemove::SuggestsImportant false;" >> /etc/apt/apt.conf.d/00recommends

COPY build-dep.yml /tmp/
COPY kmod.yml /tmp/
COPY ci.yml /tmp/

# Install dependency packages with Ansible
RUN apt-get -q update && \
	apt-get install -y ansible curl && \
        ansible-playbook /tmp/ci.yml -i "localhost," -c local && \
	apt-get purge -y ansible && \
	apt-get autoremove -y && \
	rm -rf /var/lib/apt/lists
RUN update-alternatives --install /usr/local/bin/python python /usr/bin/python3 3
RUN mkdir -p /build/bess

# Build DPDK testpmd (used in bessctl samples)
ARG BESS_DPDK_BRANCH=master
RUN cd /build/bess && \
	curl -s -L https://github.com/NetSys/bess/archive/${BESS_DPDK_BRANCH}.tar.gz | tar zx --strip-components=1 && \
	./build.py dpdk && \
	cp /build/bess/deps/dpdk-20.11.3/build/app/dpdk-testpmd /usr/local/bin/ && \
	rm -rf /build/bess

ENV CCACHE_DIR=/tmp/ccache
ENV CCACHE_COMPRESS=true

WORKDIR /build/bess
