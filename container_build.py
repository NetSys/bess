#!/usr/bin/env python

# Copyright (c) 2014-2016, The Regents of the University of California.
# Copyright (c) 2016-2017, Nefeli Networks, Inc.
# Copyright (c) 2017, Cloudigo.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# * Neither the names of the copyright holders nor the names of their
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

from __future__ import print_function
import sys
import subprocess
import os
import os.path

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


def print_usage():
    print('Usage: %s '
          '[all|bess|kmod|kmod_buildtest|clean|dist_clean|shell||help]'
          % sys.argv[0], file=sys.stderr)


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
            print('Error - unknown command "%s".' % sys.argv[1],
                  file=sys.stderr)
            print_usage()
            sys.exit(2)
    else:
        print_usage()
        sys.exit(2)


if __name__ == '__main__':
    main()
    print('Done.')
