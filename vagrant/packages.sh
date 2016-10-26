#!/bin/sh

apt-get update
apt-get upgrade -y

# basics
apt-get install -y git build-essential libpcap-dev zlib1g-dev
apt-get install -y gdb gdbserver exuberant-ctags cscope vim emacs

# perftools
apt-get install -y linux-virtual linux-tools-common linux-tools-generic \
	linux-image-extra-virtual

# grpc
apt-get install -y libtool autoconf libgoogle-glog-dev libgtest-dev libunwind8-dev liblzma-dev
git clone -q -b $(curl -s -L http://grpc.io/release) https://github.com/grpc/grpc /opt/grpc
cd /opt/grpc
git submodule update --init
make
make install
cd third_party/protobuf
make install

# bessctl
apt-get install -y python-scapy libgraph-easy-perl docker.io
