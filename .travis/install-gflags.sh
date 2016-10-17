#!/bin/bash

wget https://github.com/gflags/gflags/archive/v2.1.2.tar.gz
tar zxvf v2.1.2.tar.gz
cd gflags-2.1.2
mkdir build && cd build
cmake ..
make && sudo make install
