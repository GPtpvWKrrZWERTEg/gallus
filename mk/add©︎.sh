#!/bin/sh

find . -type f \( \
     -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o \
     -name '*.in' -o -name '*.ac' \) -print0 | \
    xargs -0 ./mk/add-copyright-header

