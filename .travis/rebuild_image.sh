#!/bin/bash

set -e

REPOSITORY=${REPOSITORY:-nefelinetworks/bess_build}
VERSION=${VERSION:-`date +%y%m%d`}

cp ../vagrant/bess.yml .
docker build --build-arg BESS_DPDK_BRANCH=${BESS_DPDK_BRANCH:-master} -t $REPOSITORY:latest -t $REPOSITORY:$VERSION .
rm *.yml

echo Build succeeded: $REPOSITORY:$VERSION
echo Build succeeded: $REPOSITORY:latest

read -p "Do you wish to push the image? [y/N] " yn
if [[ $yn =~ ^([yY][eE][sS]|[yY])$ ]]
then
    docker login;
    docker push $REPOSITORY:latest
    docker push $REPOSITORY:$VERSION
else
    echo "The image was not pushed";
fi
