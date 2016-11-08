#!/bin/sh

# Run all tests
./all_test

# Run gcov via lcov
lcov --capture --directory . --output-file coverage.info

# Generate output html
genhtml coverage.info --output-directory coverage_html
