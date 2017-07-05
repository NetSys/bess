#!/bin/bash

# Copyright (c) 2016-2017, Nefeli Networks, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# * Neither the names of the copyright holders nor the names of their
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

CXX_FORMAT="clang-format"
PY_FORMAT="autopep8"

check_directory() {
  if [ ! -d $1 ]; then
    echo "Directory $1 not found."
    echo "Please make sure you are running this script in the root directory of BESS repo."
    exit 1
  fi
}

command_exists() {
  command -v $1 >/dev/null 2>&1
}

check_directory .git
check_directory .hooks

if ! command_exists $CXX_FORMAT; then
  echo "Error: $CXX_FORMAT executable not found.\n" >&2
  exit 1
fi

if ! command_exists $PY_FORMAT; then
  echo "Error: $PY_FORMAT executable not found. Try 'pip install $PY_FORMAT'?\n" >&2
  exit 1
fi

for i in .hooks/*; do
  rm -rf .git/hooks/$(basename ${i})
  ln -s ../../${i} .git/hooks/$(basename ${i})
done
