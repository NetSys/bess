#!/bin/sh

cd /opt/bess
PYTHONUNBUFFERED=1 ./build.py dist_clean
PYTHONUNBUFFERED=1 ./build.py
