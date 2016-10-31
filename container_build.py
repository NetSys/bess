#!/usr/bin/env python

import sys
import subprocess
import os
import os.path
import time

IMAGE = 'nefelinetworks/bess_build:latest'
BESS_DIR_HOST = os.path.dirname(os.path.abspath(__file__))
BESS_DIR_CONTAINER = '/build/bess'
BUILD_SCRIPT = './build.py'

def run_cmd(cmd):
    proc = subprocess.Popen(cmd,
            shell=True)

    # err should be None
    out, err = proc.communicate()

    if proc.returncode:
        print >> sys.stderr, 'Error has occured running host command: %s' % cmd
        sys.exit(proc.returncode)

def run_docker_cmd(cmd):
    run_cmd('docker run -e CC -e CXX --rm -t -v %s:%s %s %s' % \
            (BESS_DIR_HOST, BESS_DIR_CONTAINER, IMAGE, cmd))

def build_bess():
    run_docker_cmd('%s bess' % BUILD_SCRIPT)

def build_kmod():
    kernel_ver = subprocess.check_output('uname -r', shell=True).strip()

    try:
        print 'Trying module build with the host kernel %s (optional)' % \
                kernel_ver
        run_docker_cmd('%s kmod' % BUILD_SCRIPT)
    except:
        print >> sys.stderr, '*** module build has failed.'

def build_kmod_buildtest():
    kernels_to_test = '/usr/src/linux-headers-*-generic'
    run_docker_cmd("sh -c 'ls -d %s' | xargs -n 1 " \
                   "sh -c 'echo Building kernel module: $0; " \
                          "KERNEL_DIR=$0 %s kmod'" % \
                   (kernels_to_test, BUILD_SCRIPT))

def build_all():
    build_bess()
    build_kmod()

def do_clean():
    run_docker_cmd('%s clean' % BUILD_SCRIPT)

def do_dist_clean():
    run_docker_cmd('%s dist_clean' % BUILD_SCRIPT)

def print_usage():
    print >> sys.stderr, \
            'Usage: %s [all|bess|kmod|kmod_buildtest|clean|dist_clean|help]' % \
            sys.argv[0]

def main():
    os.chdir(BESS_DIR_HOST)

    if len(sys.argv) == 1:
        build_bess()
        build_kmod()
    elif len(sys.argv) == 2:
        if sys.argv[1] == 'all':
            build_all()
        elif sys.argv[1] == 'bess':
            build_bess()
        elif sys.argv[1] == 'kmod':
            build_kmod()
        elif sys.argv[1] == 'kmod_buildtest':
            build_kmod_buildtest()
        elif sys.argv[1] == 'clean':
            do_clean()
        elif sys.argv[1] == 'dist_clean':
            do_dist_clean()
        elif sys.argv[1] == 'help':
            print_usage()
            sys.exit(0)
        else:
            print >> sys.stderr, 'Error - unknown command "%s".' % sys.argv[1]
            print_usage()
            sys.exit(2)
    else:
        print_usage()
        sys.exit(2)

if __name__ == '__main__':
    try:
        main()
        print 'Done.'

    finally:
        run_docker_cmd('chown --reference=. . -R')
