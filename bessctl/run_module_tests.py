#!/usr/bin/env python

# Copyright (c) 2017, The Regents of the University of California.
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

import argparse
import fnmatch
import glob
import os
import shlex
import subprocess
import sys
import unittest

this_dir = os.path.dirname(os.path.abspath(__file__))
bessctl = os.path.join(this_dir, 'bessctl')
default_test_dir = os.path.join(this_dir, 'module_tests')


class CommandError(subprocess.CalledProcessError):

    '''Identical to CalledProcessError, except it also shows the output'''

    def __str__(self):
        return '%s\n%s' % (super(CommandError, self).__str__(), self.output)


def run_cmd(cmd):
    args = shlex.split(cmd)
    try:
        ret = subprocess.check_call(args, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        raise CommandError(e.returncode, e.cmd, e.output)


def main():

    arg_parser = argparse.ArgumentParser(
        description='Run per-module unit tests')
    arg_parser.add_argument('--test_name', type=str, default='*',
                            help='Name of a specific test to run.')
    arg_parser.add_argument('--test_dir', type=str, default=default_test_dir,
                            help='Path to the directory to serach for tests.')
    args = arg_parser.parse_args()

    any_failure = 0

    try:
        run_cmd('%s daemon start -m 0' % bessctl)
    except CommandError:
        raise Exception('bess daemon could not start')

    for file_name in glob.glob(os.path.join(args.test_dir, "{}.py".format(args.test_name))):
        print('Running test %s' % file_name)

        try:
            run_cmd('%s daemon reset -- run file %s' % (bessctl, file_name))
        except CommandError:
            any_failure = 1
            run_cmd('%s daemon start -m 0' % bessctl)

    sys.exit(any_failure)


if __name__ == '__main__':
    main()
