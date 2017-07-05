# Copyright (c) 2014-2016, The Regents of the University of California.
# Copyright (c) 2016-2017, Nefeli Networks, Inc.
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
import fnmatch
import glob
import os
import shlex
import subprocess
import sys
import time
import unittest


this_dir = os.path.dirname(os.path.realpath(__file__))
bessctl = os.path.join(this_dir, 'bessctl')
sample_dir = os.path.join(this_dir, 'conf/samples')


class CommandError(subprocess.CalledProcessError):

    '''Identical to CalledProcessError, except it also shows the output'''

    def __str__(self):
        return '%s\n%s' % (super(CommandError, self).__str__(), self.output)


def run_cmd(cmd):
    args = shlex.split(cmd)
    try:
        subprocess.check_output(args, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        raise CommandError(e.returncode, e.cmd, e.output)


class TestSamples(unittest.TestCase):

    """
    All scripts in conf/samples will be dynamically added here as individual
    tests (e.g., test_path_to_conf_samples_exactmatch_bess) in this class.
    Each script will be executed for 0.5 second.
    NOTE: make sure that the scripts do not require any special configurations
          (physical/virtual ports, docker, etc.) and do not run interactively.
    """

    @classmethod
    def setUpClass(cls):
        run_cmd('%s daemon start' % bessctl)

    @classmethod
    def tearDownClass(cls):
        run_cmd('%s daemon stop' % bessctl)


def generate_test_method(path):
    def template(self):
        try:
            run_cmd('%s daemon reset -- run file %s' % (bessctl, path))
        except CommandError:
            # bessd may have crashed. Relaunch for next tests.
            run_cmd('%s daemon start' % bessctl)
            raise

        # 0.5 seconds should be enough to detect packet leaks in the datapath
        time.sleep(0.5)

    return template


for root, _, file_names in os.walk(sample_dir):
    for file_name in fnmatch.filter(file_names, "*.bess"):
        path = os.path.join(root, file_name)
        name = 'test' + path.replace('/', '_').replace('.', '_')
        method = generate_test_method(path)
        setattr(TestSamples, name, method)

if __name__ == '__main__':
    unittest.main()
