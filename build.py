#!/usr/bin/env python2.7

import sys
import os
import os.path
import time
import urllib
import subprocess
import textwrap

BESS_DIR = os.path.dirname(os.path.abspath(__file__))

DEPS_DIR = '%s/deps' % BESS_DIR

DPDK_REPO = 'http://dpdk.org/browse/dpdk/snapshot'
DPDK_VER = 'dpdk-16.07'

arch = subprocess.check_output('uname -m', shell=True).strip()
if arch == 'x86_64':
    DPDK_TARGET = 'x86_64-native-linuxapp-gcc'
elif arch == 'i686':
    DPDK_TARGET = 'i686-native-linuxapp-gcc'
else:
    assert False, 'Unsupported arch %s' % arch

DPDK_DIR = '%s/%s' % (DEPS_DIR, DPDK_VER)
DPDK_URL = '%s/%s.tar.gz' % (DPDK_REPO, DPDK_VER)
DPDK_FILE = '%s/%s.tar.gz' % (DEPS_DIR, DPDK_VER)
DPDK_CFLAGS = '"-g -w"'
DPDK_ORIG_CONFIG = '%s/config/common_linuxapp' % DPDK_DIR
DPDK_BASE_CONFIG = '%s/%s_common_linuxapp' % (DEPS_DIR, DPDK_VER)
DPDK_FINAL_CONFIG = '%s/%s_common_linuxapp_final' % (DEPS_DIR, DPDK_VER)

extra_libs = set()

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

def cmd_success(cmd):
    try:
        subprocess.check_call(cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                shell=True)
        return True
    except subprocess.CalledProcessError:
        return False

def check_c_header(header_file):
    test_c_file = '%s/test.c' % DEPS_DIR
    test_o_file = '%s/test.o' % DEPS_DIR

    src = """
        #include <%s>

        int main()
        {
            return 0;
        }
        """ % header_file

    try:
        with open(test_c_file, 'w') as fp:
            fp.write(textwrap.dedent(src))

        return cmd_success('gcc -c %s -o %s' % (test_c_file, test_o_file))
    finally:
        cmd('rm -f %s %s' % (test_c_file, test_o_file))

def check_c_lib(lib):
    test_c_file = '%s/test.c' % DEPS_DIR
    test_e_file = '%s/test' % DEPS_DIR

    src = """
        int main()
        {
            return 0;
        }
        """

    try:
        with open(test_c_file, 'w') as fp:
            fp.write(textwrap.dedent(src))

        return cmd_success('gcc %s -l%s -o %s' % \
                (test_c_file, lib, test_e_file))
    finally:
        cmd('rm -f %s %s' % (test_c_file, test_e_file))

def required(header_file, lib_name):
    if not check_c_header(header_file):
        print >> sys.stderr, 'Error - #include <%s> failed. ' \
                'Did you install "%s" package?' % (header_file, lib_name)
        sys.exit(1)

def check_essential():
    if not cmd_success('gcc -v'):
        print >> sys.stderr, 'Error - "gcc" is not available'
        sys.exit(1)

    if not cmd_success('make -v'):
        print >> sys.stderr, 'Error - "make" is not available'
        sys.exit(1)

    required('pcap/pcap.h', 'libpcap-dev')

def set_config(filename, config, new_value):
    with open(filename) as fp:
        lines = fp.readlines()

    with open(filename, 'w') as fp:
        for line in lines:
            if line.startswith(config + '='):
                line = '%s=%s\n' % (config, new_value)
            fp.write(line)

def check_bnx():
    if check_c_header('zlib.h') and check_c_lib('z'):
        global extra_libs
        extra_libs.add('z')
    else:
        print ' - "zlib1g-dev" is not available. ' \
                'Disabling BNX2X PMD...'
        set_config(DPDK_FINAL_CONFIG, 'CONFIG_RTE_LIBRTE_BNX2X_PMD', 'n')

def check_mlx():
    if check_c_header('infiniband/verbs_exp.h'):
        global extra_libs
        extra_libs.add('ibverbs')
        #extra_libs.add('mlx5')
    else:
        print ' - "Mellanox OFED" is not available. ' \
                'Disabling MLX4 and MLX5 PMDs...'
        if check_c_header('infiniband/verbs.h'):
            print '   NOTE: "libibverbs-dev" does exist, but it does not ' \
                    'work with MLX PMDs. Instead download OFED from ' \
                    'http://www.melloanox.com'
        set_config(DPDK_FINAL_CONFIG, 'CONFIG_RTE_LIBRTE_MLX4_PMD', 'n')
        set_config(DPDK_FINAL_CONFIG, 'CONFIG_RTE_LIBRTE_MLX5_PMD', 'n')

def generate_extra_mk():
    global extra_libs

    with open('core/extra.mk', 'w') as fp:
        fp.write('LIBS += %s ' % \
                ' '.join(map(lambda lib: '-l' + lib, extra_libs)))

def download_dpdk():
    try:
        print 'Downloading %s ...  ' % DPDK_URL,
        if sys.stdout.isatty():
            urllib.urlretrieve(DPDK_URL, DPDK_FILE, reporthook=download_hook)
            print
        else:
            print
            urllib.urlretrieve(DPDK_URL, DPDK_FILE)
    except:
        cmd('rm -f %s' % (DPDK_FILE))
        raise

def configure_dpdk():
    try:
        print 'Configuring DPDK...'
        cmd('cp -f %s %s' % (DPDK_BASE_CONFIG, DPDK_FINAL_CONFIG))

        check_bnx()
        check_mlx()

        generate_extra_mk()

        cmd('cp -f %s %s' % (DPDK_FINAL_CONFIG, DPDK_ORIG_CONFIG))
        cmd('make -C %s config T=%s' % (DPDK_DIR, DPDK_TARGET))
    finally:
        cmd('rm -f %s' % DPDK_FINAL_CONFIG)

def setup_dpdk():
    check_essential()

    if not os.path.exists(DPDK_DIR):
        if not os.path.exists(DPDK_FILE):
            download_dpdk()

        try:
            print 'Decompressing DPDK...'
            cmd('mkdir -p %s' % DPDK_DIR)
            cmd('tar zxf %s -C %s --strip-components 1' % (DPDK_FILE, DPDK_DIR))
        except:
            cmd('rm -rf %s' % (DPDK_DIR))
            raise
        finally:
            cmd('rm -f %s' % (DPDK_FILE))

    # not configured yet?
    if not os.path.exists('%s/build' % DPDK_DIR):
        configure_dpdk()

    print 'Building DPDK...'
    nproc = int(subprocess.check_output('nproc'))
    cmd('make -j%d -C %s EXTRA_CFLAGS=%s' % \
            (nproc, DPDK_DIR, DPDK_CFLAGS))

def build_bess():
    check_essential()

    print 'Building BESS daemon...'
    cmd('bin/bessctl daemon stop 2> /dev/null || true')
    cmd('rm -f core/bessd')     # force relink as DPDK might have been rebuilt
    cmd('make -C core')
    cmd('ln -f -s ../core/bessd bin/bessd')

def build_kmod():
    check_essential()

    print 'Building BESS Linux kernel module... (optional)'
    cmd('sudo -n rmmod bess 2> /dev/null || true')
    try:
        cmd('make -C core/kmod')
    except SystemExit:
        print >> sys.stderr, '*** module build has failed.'
        raise

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
            'Usage: %s [all|bess|kmod|clean|dist_clean|help]' % \
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
