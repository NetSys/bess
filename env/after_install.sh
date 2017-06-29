#!/bin/bash

killall -q bessd
sleep 1
if lsmod | grep -q bess; then
  rmmod bess || exit 0
fi

make -C /usr/local/bess/core/kmod && insmod /usr/local/bess/core/kmod/bess.ko && echo "Module successfully installed!"
chmod 0444 /dev/bess
perf buildid-cache -a /usr/local/bess/core/kmod/bess.ko

ln -sf /usr/local/bess/core/bessd /usr/local/bin/bessd
ln -sf /usr/local/bess/bessctl/bessctl /usr/local/bin/bessctl
ln -sf /usr/local/bess/bin/dpdk_devbind.py /usr/local/bin/dpdk_devbind.py
