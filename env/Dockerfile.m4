# vim: syntax=dockerfile

FROM BASE_IMAGE

# Install Ansible
RUN apt-get -q update
RUN apt-get install -y software-properties-common
RUN apt-add-repository -y ppa:ansible/ansible
RUN apt-get -q update
RUN apt-get install -y ansible

COPY packages.yml /tmp/packages.yml
RUN ansible-playbook /tmp/packages.yml -i "localhost," -c local && rm -rf /tmp/*

RUN mkdir -p /build

# Pre-build DPDK from the specified BESS branch
ARG BESS_DPDK_BRANCH
ARG DPDK_ARCH
RUN cd /build && \
	git clone -b ${BESS_DPDK_BRANCH} https://github.com/netsys/bess && \
	cd /build/bess && \
	setarch ${DPDK_ARCH} ./build.py dpdk && \
	mv /build/bess/deps/dpdk-17.02 /build/dpdk-17.02 && \
	rm -rf /build/bess

WORKDIR /build/bess
