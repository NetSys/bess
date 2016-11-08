#!/bin/sh

case $1 in
    compute)
        # Run all tests
        ./all_test

        # Run gcov via lcov
        lcov --capture --directory . --output-file coverage.info

        # Generate output html
        genhtml coverage.info --output-directory coverage_html
        ;;
    serve)
        echo "Serving coverage UI at http://localhost:8000"
        cd coverage-html-output && python -m SimpleHTTPServer 8000
        ;;
    *)
        echo "Usage: $0 {compute|serve}"
        ;;
esac
