#!/bin/bash

wget https://github.com/google/glog/archive/v0.3.4.tar.gz
tar zxvf glog-0.3.4.tar.gz
cd glog-0.3.4
./configure
make && sudo make install
