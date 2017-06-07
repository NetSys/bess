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


def test_generator(path):
    def test(self):
        run_cmd('%s daemon reset -- run file %s' % (bessctl, path))

        # 0.5 seconds should be enough to detect packet leaks in the datapath
        time.sleep(0.5)

    return test


for root, _, file_names in os.walk(sample_dir):
    for file_name in fnmatch.filter(file_names, "*.bess"):
        path = os.path.join(root, file_name)
        test_name = 'test' + path.replace('/', '_').replace('.', '_')
        test_method = test_generator(path)
        setattr(TestSamples, test_name, test_method)

if __name__ == '__main__':
    unittest.main()
