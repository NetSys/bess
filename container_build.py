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
import re
import argparse

IMAGE = os.getenv('IMAGE', 'nefelinetworks/bess_build') + ':' + os.getenv('TAG_SUFFIX', 'latest')
BESS_DIR_HOST = os.path.dirname(os.path.abspath(__file__))
BESS_DIR_CONTAINER = '/build/bess'
BUILD_SCRIPT = './build.py'
PLUGINS = []

CCACHE_DIR_HOST = '~/.ccache'
CCACHE_DIR_CONTAINER = '/tmp/ccache'


def docker_mount_args(plugins):
    """
    Return the string of arguments to "docker run" suitable for
    mounting BESS_DIR_HOST (as BESS_DIR_CONTAINER), ccache, and
    all the plugins under their original paths.

    Note, the plugins must not live under the BESS_DIR_CONTAINER
    directory itself since that will not play well.  But plugins
    can live under a previously-exported path, including the
    BESS directory itself -- this is the case for the sample
    plugin, for instance.
    """
    for path in plugins:
        if not os.path.isabs(path):
            sys.exit('Error: plugin {!r} path is not absolute'.format(path))
        if path.startswith(BESS_DIR_CONTAINER + os.path.sep):
            sys.exit('Error: plugin {!r}: path {!r} is taken by '
                     'bess'.format(path, BESS_DIR_CONTAINER))

    # If the ccache directory does not exist, create one owned by the current
    # user (otherwise Docker will create one with root:root)
    os.system('mkdir -p %s' % CCACHE_DIR_HOST)

    ret = ['-v {}:{}'.format(CCACHE_DIR_HOST, CCACHE_DIR_CONTAINER),
           '-v {}:{}'.format(BESS_DIR_HOST, BESS_DIR_CONTAINER)]

    # Make sure longer paths that are prefixes of shorter paths
    # appear after the shorter paths.  That way we mount only
    # /foo/bar if both /foo/bar and /foo/bar/sub/dir are listed.
    earlier = {BESS_DIR_HOST: True}
    for path in sorted(plugins, key=lambda x: x + os.path.sep):
        # given, e.g., /foo/bar/baz we want to look at /foo, /foo/bar,
        # and /foo/bar/baz to see if they're already in the mount list.
        components = path.split(os.path.sep)
        components[0] = os.path.sep
        for i in range(1, len(components)):
            if os.path.join(*components[:i + 1]) in earlier:
                # already mounted
                break
        else:
            # no prefix is mounted yet
            ret.append('-v {path}:{path}'.format(path=path))
            earlier[path] = True
    return ' '.join(ret)


def run_cmd(cmd):
    proc = subprocess.Popen(cmd, shell=True)

    proc.communicate()

    if proc.returncode:
        print('Error has occured running host command: %s' % cmd,
              file=sys.stderr)
        sys.exit(proc.returncode)


def shell_quote(cmd):
    return "'" + cmd.replace("'", "'\\''") + "'"


def docker_env_args():
    env_vars = ['V', 'CXX', 'DEBUG', 'SANITIZE']
    return ' '.join(['-e %s' % var for var in env_vars])


def run_docker_cmd(cmd):
    run_cmd('docker run %s --rm -t -u %d:%d %s %s sh -c %s' %
            (docker_env_args(), os.getuid(), os.getgid(),
             docker_mount_args(PLUGINS), IMAGE, shell_quote(cmd)))


def run_shell():
    run_cmd('docker run %s --rm -it %s %s' %
            (docker_env_args(), docker_mount_args(PLUGINS), IMAGE))


def find_current_plugins():
    "return list of existing plugins"
    result = []
    try:
        for line in open('core/extra.mk').readlines():
            match = re.match(r'PLUGINS \+= (.*)', line)
            if match:
                result.append(match.group(1))
    except (OSError, IOError):
        pass
    return result


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
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='pass verbose flag to build inside container')

    args = parser.parse_args()
    if args.verbose:
        os.environ['V'] = '1'

    PLUGINS.extend(find_current_plugins())

    cmds[args.action]()


if __name__ == '__main__':
    main()
    print('Done.')
