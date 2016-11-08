#!/bin/bash

case $1 in
    compute)
        ./all_test

        # Run gcov via lcov
        lcov --capture --directory . --output-file coverage.info

        # Generate output html
        genhtml coverage.info --output-directory coverage-html-output
        ;;
    serve)
        echo "Serving coverage UI at http://localhost:8000"
        cd coverage-html-output && python -m SimpleHTTPServer 8000
        ;;
    *)
        echo "Usage: $0 {compute|serve}"
        ;;
esac
