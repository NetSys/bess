#!/bin/bash

# Run all tests
for a in *_test */*_test
do
  ./$a
done

# Run gcov via lcov
lcov --capture --directory . --output-file coverage.info

# Generate output html
genhtml coverage.info --output-directory coverage-html-output
