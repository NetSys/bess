#!/usr/bin/env python2.7

import sys
import os
import os.path
import time
import urllib
import subprocess

BESS_DIR = os.path.dirname(os.path.abspath(__file__))

DEPS_DIR = '%s/deps' % BESS_DIR 

DPDK_REPO = 'http://dpdk.org/browse/dpdk/snapshot'
DPDK_VER = 'dpdk-2.0.0'

DPDK_DIR = '%s/dpdk' % DEPS_DIR
DPDK_URL = '%s/%s.tar.gz' % (DPDK_REPO, DPDK_VER)
DPDK_FILE = '%s/%s.tar.gz' % (DEPS_DIR, DPDK_VER)
DPDK_CFLAGS = '"-g -Wno-error=all"'

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

def download_dpdk():
    try:
        print 'Downloading %s ...  ' % DPDK_URL,
        urllib.urlretrieve(DPDK_URL, DPDK_FILE, reporthook=download_hook) 
        print
    except:
        cmd('rm -f %s' % (DPDK_FILE))
        raise

def setup_dpdk():
    if not os.path.exists(DPDK_DIR):
        if not os.path.exists(DPDK_FILE):
            download_dpdk()
            
        try:
            print 'Decompressing DPDK...'
            cmd('mkdir -p %s' % DPDK_DIR)
            cmd('tar zxf %s -C %s --strip-components 1' % (DPDK_FILE, DPDK_DIR))
        except:
            cmd('rm -rf %s' % (DPDK_DIR))

    # not configured yet?
    if not os.path.exists('%s/build' % DPDK_DIR):
        print 'Configuring DPDK...'
        cmd('cp -f %s/%s_common_linuxapp %s/config/common_linuxapp' % \
                (DEPS_DIR, DPDK_VER, DPDK_DIR))
        cmd('make -C %s config T=x86_64-native-linuxapp-gcc' % DPDK_DIR)

    print 'Building DPDK...'
    nproc = int(subprocess.check_output('nproc'))
    cmd('make -j%d -C %s EXTRA_CFLAGS=%s' % \
            (nproc, DPDK_DIR, DPDK_CFLAGS))

def build_bess():
    print 'Building BESS daemon...'
    cmd('make -C core')
    cmd('ln -f -s ../core/bessd bin/bessd')

def build_kmod():
    print 'Building BESS Linux kernel module... (optional)'
    try:
        cmd('make -C core/kmod')
    except SystemExit:
        print >> sys.stderr, '*** module build has failed.'

def build_all():
    setup_dpdk()
    build_bess()
    build_kmod()

def do_clean():
    print 'Cleaning up...'
    cmd('make -C core clean')
    cmd('rm -f bin/bessd')
    cmd('make -C core/kmod clean')

def do_dist_clean():
    do_clean()
    print 'Removing 3rd-party libraries...'
    cmd('rm -rf %s %s' % (DPDK_FILE, DPDK_DIR))

def print_usage():
    print >> sys.stderr, \
            'Usage: python %s [all|bess|kmod|clean|dist_clean|help]' % \
            sys.argv[0]
    sys.exit(2)

def main():
    os.chdir(BESS_DIR)

    if len(sys.argv) == 1:
        build_all()
    elif len(sys.argv) == 2:
        if sys.argv[1] == 'all':
            build_all()
        elif sys.argv[1] == 'bess':
            build_bess()
        elif sys.argv[1] == 'kmod':
            build_kmod()
        elif sys.argv[1] == 'clean':
            do_clean()
        elif sys.argv[1] == 'dist_clean':
            do_dist_clean()
        elif sys.argv[1] == 'help':
            print_usage()
        else:
            print >> sys.stderr, 'Error - unknown command "%s".' % sys.argv[1]
            print_usage()
    else:
        print_usage()

    print 'Done.'

if __name__ == '__main__':
    main()
