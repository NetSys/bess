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
    proc = subprocess.Popen(cmd, shell=True)

    # err should be None
    out, err = proc.communicate()

    if proc.returncode:
        print >> sys.stderr, 'Error has occured running host command: %s' % cmd
        sys.exit(proc.returncode)


def shell_quote(cmd):
        return "'" + cmd.replace("'", "'\\''") + "'"


def run_docker_cmd(cmd):
    run_cmd('docker run -e CXX -e DEBUG --rm -t -u %d:%d -v %s:%s %s sh -c %s' %
            (os.getuid(), os.getgid(), BESS_DIR_HOST, BESS_DIR_CONTAINER, IMAGE, shell_quote(cmd)))


def run_shell():
    run_cmd('docker run -e CXX -e DEBUG --rm -it -v %s:%s %s' %
            (BESS_DIR_HOST, BESS_DIR_CONTAINER, IMAGE))


def build_bess():
    run_docker_cmd('%s bess' % BUILD_SCRIPT)


def build_kmod():
    kernel_ver = subprocess.check_output('uname -r', shell=True).strip()

    try:
        run_docker_cmd('%s kmod' % BUILD_SCRIPT)
    except:
        print >> sys.stderr, '*** module build has failed.'


def build_kmod_buildtest():
    kernels_to_test = '/lib/modules/*/build'
    kmod_build = 'KERNELDIR=$0 %s kmod' % BUILD_SCRIPT

    run_docker_cmd('ls -x -d %s | xargs -n 1 sh -c %s' %
                   (kernels_to_test, shell_quote(kmod_build)))


def build_all():
    build_bess()
    build_kmod()


def do_clean():
    run_docker_cmd('%s clean' % BUILD_SCRIPT)


def do_dist_clean():
    run_docker_cmd('%s dist_clean' % BUILD_SCRIPT)


def print_usage():
    print >> sys.stderr, \
            'Usage: %s ' \
            '[all|bess|kmod|kmod_buildtest|clean|dist_clean|shell||help]' % \
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
        elif sys.argv[1] == 'shell':
            run_shell()
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
    main()
    print 'Done.'
