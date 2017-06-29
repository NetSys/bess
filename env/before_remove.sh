#!/bin/bash

killall -q bessd
sleep 1

if lsmod | grep -q bess; then
  rmmod bess || exit 0
fi

rm -rf /usr/local/bin/bessd /usr/local/bin/bessctl /usr/local/bin/dpdk_devbind.py
[[ ! -d "/usr/local/bess/kmod" ]] || (cd /usr/local/bess/kmod && rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c *.symvers .tmp_versions)
