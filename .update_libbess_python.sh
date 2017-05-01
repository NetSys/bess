#!/bin/bash

## This script should be called by Travis after a merge
## is performed on the master branch. It updates
## the libbess-python library for standalone use (without
## the full bess repository).

if [ $TRAVIS_BRANCH != "master" ]
then
  echo "This is not the master branch. No need to update libbess-python."
  exit 0
fi

## Tests are done -- clean up
bessdir=`pwd`
./build.py clean

## Decrypt the github deploy key
openssl aes-256-cbc -K $encrypted_7d74e25dfc64_key -iv $encrypted_7d74e25dfc64_iv -in .libbess_robot.enc -out .libbess_robot -d
cp .libbess_robot ~/.ssh/id_rsa
chmod 600 ~/.ssh/id_rsa

## Set up SSH
ssh-add .libbess_robot
echo "StrictHostKeyChecking no" >~/.ssh/config

## Pull the latest libbess-python repo
mkdir -p ~/libbess-python
git clone git@github.com:nefeli/libbess-python.git ~/libbess-python
cd ~/libbess-python
cp -R $bessdir/libbess-python/* .
git add --all
git commit -am "this is an automated push from a robot"
git push
