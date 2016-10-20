#!/bin/bash

cd /tmp
if [[ ! -d glog-0.3.4 ]]; then
  wget https://github.com/google/glog/archive/v0.3.4.tar.gz
  tar zxvf v0.3.4.tar.gz
fi
cd glog-0.3.4
./configure
make && sudo make install
