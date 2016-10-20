#!/bin/bash

cd /tmp
if [[ ! -d grpc ]]; then
  git clone -b $(curl -L http://grpc.io/release) https://github.com/grpc/grpc
fi
cd grpc
git submodule update --init
make && sudo make install
cd third_party/protobuf
make && sudo make install
sudo ldconfig
