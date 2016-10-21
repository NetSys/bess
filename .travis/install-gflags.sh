#!/bin/bash

cd /tmp
wget https://github.com/gflags/gflags/archive/v2.1.2.tar.gz
tar zxvf v2.1.2.tar.gz
cd gflags-2.1.2
mkdir -p build && cd build
cmake ..
make && sudo make install
