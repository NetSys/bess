#!/bin/sh

apt-get update
apt-get upgrade -y
apt-get install -y build-essential libpcap-dev build-essential
apt-get install -y linux-virtual linux-tools-common linux-tools-generic \
	linux-image-extra-virtual zlib1g-dev gdb gdbserver python-scapy \
	libgraph-easy-perl exuberant-ctags cscope vim emacs
apt-get install -y docker.io
