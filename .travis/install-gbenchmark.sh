#!/bin/bash

cd /tmp
wget https://github.com/google/benchmark/archive/v1.0.0.tar.gz
tar zxvf v1.0.0.tar.gz
cd benchmark-1.0.0
cmake .
make
sudo make install
