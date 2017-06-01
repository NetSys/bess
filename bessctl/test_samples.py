import fnmatch
import os
import shlex
import subprocess
import sys
import time
import unittest

try:
    this_dir = os.path.dirname(os.path.realpath(__file__))
    sys.path.insert(1, '%s/../libbess-python' % this_dir)
    from bess import *
except ImportError:
    print >> sys.stderr, 'Cannot import the API module (libbess-python)'
    raise


def run_cmd(cmd):
    args = shlex.split(cmd)
    subprocess.check_output(args, stderr=subprocess.STDOUT)


class TestSamples(unittest.TestCase):
    """
    All scripts in conf/samples will be dynamically added here as individual
    tests (e.g., test_conf_samples_exactmatch_bess) in this class.
    Each script will be executed for 0.5 second.
    NOTE: make sure that the scripts do not require any special configurations
          (physical/virtual ports, docker, etc.) and do not run interactively.
    """

    @classmethod
    def setUpClass(cls):
        run_cmd('./bessctl daemon start')

    @classmethod
    def tearDownClass(cls):
        run_cmd('./bessctl daemon stop')


def test_generator(path):
    def test(self):
        run_cmd('./bessctl daemon reset -- run file %s' % path)

        # 0.5 seconds should be enough to detect packet leaks in the datapath
        time.sleep(0.5)

    return test


for root, dir_names, file_names in os.walk('conf/samples'):
    for file_name in fnmatch.filter(file_names, "*.bess"):
        path = os.path.join(root, file_name)
        test_name = 'test_' + path.replace('/', '_').replace('.', '_')
        test_method = test_generator(path)
        setattr(TestSamples, test_name, test_method)

if __name__ == '__main__':
    unittest.main()
