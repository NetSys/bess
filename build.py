#!/usr/bin/env python2.7

import sys
import os
import time
import urllib
import subprocess

DEPS_DIR = './deps'

DPDK_REPO = 'http://dpdk.org/browse/dpdk/snapshot'
DPDK_VER = 'dpdk-2.0.0'

DPDK_DIR = '%s/dpdk' % DEPS_DIR
DPDK_URL = '%s/%s.tar.gz' % (DPDK_REPO, DPDK_VER)
DPDK_FILE = '%s/%s.tar.gz' % (DEPS_DIR, DPDK_VER)
DPDK_CFLAGS = '"-g -Wno-error=maybe-uninitialized"'

def download_hook(count, block_size, total_size):
    sys.stdout.write('\x08' + ['-', '\\', '|', '/'][int(time.time() * 3) % 4])
    sys.stdout.flush()
    
def cmd(cmd):
    proc = subprocess.Popen(cmd, 
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, 
            shell=True)

    # err should be None
    out, err = proc.communicate()

    if proc.returncode:
        print >> sys.stderr, 'Log:\n', out
        print >> sys.stderr, 'Error has occured running command: %s' % cmd
        sys.exit(proc.returncode)

def setup_dpdk():
    cmd('rm -rf %s %s' % (DPDK_DIR, DPDK_FILE))

    print 'Downloading %s ...  ' % DPDK_URL,
    urllib.urlretrieve(DPDK_URL, DPDK_FILE, reporthook=download_hook) 
    print

    print 'Decompressing DPDK...'
    cmd('mkdir -p %s' % DPDK_DIR)
    cmd('tar zxf %s -C %s --strip-components 1' % (DPDK_FILE, DPDK_DIR))

    print 'Configuring DPDK...'
    cmd('cp -f %s/%s_common_linuxapp %s/config/common_linuxapp' % \
            (DEPS_DIR, DPDK_VER, DPDK_DIR))
    cmd('make -C %s config T=x86_64-native-linuxapp-gcc' % DPDK_DIR)

    print 'Building DPDK...'
    cmd('make -C %s -j8 EXTRA_CFLAGS=%s' % (DPDK_DIR, DPDK_CFLAGS))

    cmd('rm -f %s' % (DPDK_FILE))

def build_bess():
    print 'Building BESS daemon...'
    cmd('make -C core')

def main():
    setup_dpdk()
    build_bess()
    print 'Build has been completed successfully'

if __name__ == '__main__':
    main()
