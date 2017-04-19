#!/bin/bash
for i in .hooks/*; do ln -s "../../${i}" ".git/hooks/$(basename ${i})"; done
