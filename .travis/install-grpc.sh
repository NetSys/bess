#!/bin/bash

cd /tmp
git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
cd grpc
git submodule update --init
make && sudo make install
cd third_party/protobuf
make && sudo make install
sudo ldconfig
