#!/usr/bin/env python

from __future__ import print_function
import sys
import subprocess
import os
import os.path
import argparse

IMAGE = 'nefelinetworks/bess_build:latest' + os.getenv('TAG_SUFFIX', '')
BESS_DIR_HOST = os.path.dirname(os.path.abspath(__file__))
BESS_DIR_CONTAINER = '/build/bess'
BUILD_SCRIPT = './build.py'


def run_cmd(cmd):
    proc = subprocess.Popen(cmd, shell=True)

    proc.communicate()

    if proc.returncode:
        print('Error has occured running host command: %s' % cmd,
              file=sys.stderr)
        sys.exit(proc.returncode)


def shell_quote(cmd):
    return "'" + cmd.replace("'", "'\\''") + "'"


def run_docker_cmd(cmd):
    run_cmd('docker run -e CXX -e DEBUG -e SANITIZE --rm -t '
            '-u %d:%d -v %s:%s %s sh -c %s' %
            (os.getuid(), os.getgid(), BESS_DIR_HOST, BESS_DIR_CONTAINER,
             IMAGE, shell_quote(cmd)))


def run_shell():
    run_cmd('docker run -e CXX -e DEBUG -e SANITIZE --rm -it -v %s:%s %s' %
            (BESS_DIR_HOST, BESS_DIR_CONTAINER, IMAGE))


def build_bess():
    run_docker_cmd('%s bess' % BUILD_SCRIPT)


def build_kmod():
    subprocess.check_output('uname -r', shell=True).strip()

    try:
        run_docker_cmd('%s kmod' % BUILD_SCRIPT)
    except:
        print('*** module build has failed.', file=sys.stderr)


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


def print_usage(parser):
    parser.print_help(file=sys.stderr)
    sys.exit(2)


def main():
    os.chdir(BESS_DIR_HOST)
    parser = argparse.ArgumentParser(description='Build BESS in container')
    cmds = {
        'all': build_all,
        'bess': build_bess,
        'kmod': build_kmod,
        'kmod_buildtest': build_kmod_buildtest,
        'clean': do_clean,
        'dist_clean': do_dist_clean,
        'shell': run_shell,
        'help': lambda: print_usage(parser),
    }
    cmdlist = sorted(cmds.keys())
    parser.add_argument(
        'action',
        metavar='action',
        nargs='?',
        default='all',
        choices=cmdlist,
        help='Action is one of ' + ', '.join(cmdlist))

    args = parser.parse_args()

    cmds[args.action]()


if __name__ == '__main__':
    main()
    print('Done.')
