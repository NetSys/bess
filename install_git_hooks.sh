#!/bin/bash

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
