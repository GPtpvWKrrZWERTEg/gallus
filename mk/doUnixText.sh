#!/bin/sh

l=`find . -type f | grep -v '.git/'`

for i in ${l}; do
    nkf -w -Lu -d ${i} > ${i}.tmp
    rm -f ${i}
    mv -f ${i}.tmp ${i}
done

exit 0


