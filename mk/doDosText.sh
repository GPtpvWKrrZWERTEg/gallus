#!/bin/sh

l=`find . -type f \( \
    -name '*.h' -o \
    -name '*.c' -o \
    -name '*.cpp' -o \
    -name 'Makefile.in' \) | grep -v '.git/'`

for i in ${l}; do
    nkf -w8 -Lw -c ${i} > ${i}.tmp
    rm -f ${i}
    mv -f ${i}.tmp ${i}
done

exit 0


