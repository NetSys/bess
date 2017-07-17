#!/usr/bin/env python

from __future__ import print_function
import sys
import os
import os.path
import subprocess
import textwrap
import argparse

BESS_DIR = os.path.dirname(os.path.abspath(__file__))

DEPS_DIR = '%s/deps' % BESS_DIR

# It's best to use a release tag if possible -- see comments in
# download_dpdk
DPDK_REPO = 'http://dpdk.org/git/dpdk'
DPDK_TAG = 'v17.05'

DPDK_VER = 'dpdk-17.05'

arch = subprocess.check_output('uname -m', shell=True).strip()
if arch == 'x86_64':
    DPDK_TARGET = 'x86_64-native-linuxapp-gcc'
elif arch == 'i686':
    DPDK_TARGET = 'i686-native-linuxapp-gcc'
else:
    assert False, 'Unsupported arch %s' % arch

DPDK_DIR = '%s/%s' % (DEPS_DIR, DPDK_VER)
DPDK_CFLAGS = '"-g -w -fPIC"'
DPDK_ORIG_CONFIG = '%s/config/common_linuxapp' % DPDK_DIR
DPDK_BASE_CONFIG = '%s/%s_common_linuxapp' % (DEPS_DIR, DPDK_VER)
DPDK_FINAL_CONFIG = '%s/%s_common_linuxapp_final' % (DEPS_DIR, DPDK_VER)

extra_libs = set()
cxx_flags = []
ld_flags = []


def cmd(cmd):
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=True)

    # err should be None
    out, err = proc.communicate()

    if proc.returncode:
        print('Log:\n', out, file=sys.stderr)
        print('Error has occured running command: %s' % cmd, file=sys.stderr)
        sys.exit(proc.returncode)


def cmd_success(cmd):
    try:
        subprocess.check_call(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, shell=True)
        return True
    except subprocess.CalledProcessError:
        return False


def check_header(header_file, compiler):
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

        return cmd_success('%s %s -c %s -o %s' %
                           (compiler, ' '.join(cxx_flags), test_c_file,
                            test_o_file))

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

        return cmd_success('gcc %s -l%s %s %s -o %s' %
                           (test_c_file, lib, ' '.join(cxx_flags),
                            ' '.join(ld_flags), test_e_file))
    finally:
        cmd('rm -f %s %s' % (test_c_file, test_e_file))


def required(header_file, lib_name, compiler):
    if not check_header(header_file, compiler):
        print('Error - #include <%s> failed. Did you install "%s" package?'
              % (header_file, lib_name), file=sys.stderr)
        sys.exit(1)


def check_essential():
    if not cmd_success('gcc -v'):
        print('Error - "gcc" is not available', file=sys.stderr)
        sys.exit(1)

    if not cmd_success('g++ -v'):
        print('Error - "g++" is not available', file=sys.stderr)
        sys.exit(1)

    if not cmd_success('make -v'):
        print('Error - "make" is not available', file=sys.stderr)
        sys.exit(1)

    required('pcap/pcap.h', 'libpcap-dev', 'gcc')
    required('zlib.h', 'zlib1g-dev', 'gcc')
    required('glog/logging.h', 'libgoogle-glog-dev', 'g++')
    required('gflags/gflags.h', 'libgflags-dev', 'g++')
    required('gtest/gtest.h', 'libgtest-dev', 'g++')
    required('benchmark/benchmark.h', 'https://github.com/google/benchmark',
             'g++')


def set_config(filename, config, new_value):
    with open(filename) as fp:
        lines = fp.readlines()

    with open(filename, 'w') as fp:
        for line in lines:
            if line.startswith(config + '='):
                line = '%s=%s\n' % (config, new_value)
            fp.write(line)


def check_bnx():
    if check_header('zlib.h', 'gcc') and check_c_lib('z'):
        global extra_libs
        extra_libs.add('z')
    else:
        print(' - "zlib1g-dev" is not available. Disabling BNX2X PMD...')
        set_config(DPDK_FINAL_CONFIG, 'CONFIG_RTE_LIBRTE_BNX2X_PMD', 'n')


def check_mlx():
    if check_header('infiniband/verbs_exp.h', 'gcc'):
        global extra_libs
        extra_libs.add('ibverbs')
        # extra_libs.add('mlx5')
    else:
        print(' - "Mellanox OFED" is not available. '
              'Disabling MLX4 and MLX5 PMDs...')
        if check_header('infiniband/verbs.h', 'gcc'):
            print('   NOTE: "libibverbs-dev" does exist, but it does not '
                  'work with MLX PMDs. Instead download OFED from '
                  'http://www.melloanox.com')
        set_config(DPDK_FINAL_CONFIG, 'CONFIG_RTE_LIBRTE_MLX4_PMD', 'n')
        set_config(DPDK_FINAL_CONFIG, 'CONFIG_RTE_LIBRTE_MLX5_PMD', 'n')


def generate_dpdk_extra_mk():
    global extra_libs

    with open('core/extra.dpdk.mk', 'w') as fp:
        fp.write(
            'LIBS += %s\n' % ' '.join(map(lambda lib: '-l' + lib, extra_libs)))


def generate_extra_mk():
    global cxx_flags
    global ld_flags
    with open('core/extra.mk', 'w') as fp:
        fp.write('CXXFLAGS += %s\n' % ' '.join(cxx_flags))
        fp.write('LDFLAGS += %s\n' % ' '.join(ld_flags))


def download_dpdk(quiet=False):
    if os.path.exists(DPDK_DIR):
        if not quiet:
            print('already downloaded to %s' % DPDK_DIR)
        return
    try:
        print('Downloading %s ...  ' % DPDK_REPO)
        # Using --depth 1 speeds download, but leaves some traps.
        # We'll fix the trickiest one here.
        #
        # Because the -b arg is a tag, we will be on a detached HEAD.
        #
        # If you need a branch instead of a tag, or need to check
        # out a commit by raw hash ID, run "git fetch unshallow" before
        # your "git checkout", or remove the "--depth 1" here and
        # remove the subsequent "git ... config remote.url.fetch"
        # command.
        #
        # If the base git is pre-2.7.4 and there is no configured
        # user.name and user.email, "git clone" can fail when run
        # with no (or not enough) of a user database, e.g., in a
        # container.  To work around this, we supply a dummy
        # user.name and user.email (which are otherwise ignored --
        # they do not wind up in the new config file).
        cmd('git -c user.name=git-bug-workaround -c user.email=not@used.com '
            'clone --depth 1 -b %s %s %s' % (DPDK_TAG, DPDK_REPO, DPDK_DIR))
        cmd("git -C %s config "
            "remote.url.fetch '+refs/heads/*:refs/remotes/origin/*'" % DPDK_DIR)

    except:
        # git removes the clone on interrupt, but let's do it here
        # in case we did not finish reconfiguring.
        cmd('rm -rf %s' % (DPDK_DIR))
        raise


def configure_dpdk():
    try:
        print('Configuring DPDK...')
        cmd('cp -f %s %s' % (DPDK_BASE_CONFIG, DPDK_FINAL_CONFIG))

        check_mlx()

        generate_dpdk_extra_mk()

        cmd('cp -f %s %s' % (DPDK_FINAL_CONFIG, DPDK_ORIG_CONFIG))
        cmd('make -C %s config T=%s' % (DPDK_DIR, DPDK_TARGET))
    finally:
        cmd('rm -f %s' % DPDK_FINAL_CONFIG)


def build_dpdk():
    check_essential()
    download_dpdk(quiet=True)

    # not configured yet?
    if not os.path.exists('%s/build' % DPDK_DIR):
        configure_dpdk()

    print('Building DPDK...')
    nproc = int(subprocess.check_output('nproc'))
    cmd('make -j%d -C %s EXTRA_CFLAGS=%s' % (nproc, DPDK_DIR, DPDK_CFLAGS))


def build_bess():
    check_essential()

    if not os.path.exists('%s/build' % DPDK_DIR):
        build_dpdk()

    generate_extra_mk()

    print('Generating protobuf codes for pybess...')
    cmd('protoc protobuf/*.proto \
        --proto_path=protobuf --python_out=pybess \
        --grpc_out=pybess \
        --plugin=protoc-gen-grpc=`which grpc_python_plugin`')
    cmd('protoc protobuf/tests/*.proto \
        --proto_path=protobuf/tests/ --python_out=pybess \
        --grpc_out=pybess \
        --plugin=protoc-gen-grpc=`which grpc_python_plugin`')
    cmd('2to3 -wn pybess/*_pb2.py')

    print('Building BESS daemon...')
    cmd('bin/bessctl daemon stop 2> /dev/null || true')
    cmd('rm -f core/bessd')  # force relink as DPDK might have been rebuilt
    cmd('make -C core -j`nproc`')
    cmd('ln -f -s ../core/bessd bin/bessd')


def build_kmod():
    check_essential()

    if os.getenv('KERNELDIR'):
        print('Building BESS kernel module (%s) ...' %
              os.getenv('KERNELDIR'))
    else:
        print('Building BESS kernel module (%s - running kernel) ...' %
              subprocess.check_output('uname -r', shell=True).strip())

    cmd('sudo -n rmmod bess 2> /dev/null || true')
    try:
        cmd('make -C core/kmod')
    except SystemExit:
        print('*** module build has failed.', file=sys.stderr)
        sys.exit(1)


def build_all():
    build_dpdk()
    build_bess()
    build_kmod()
    print('Done.')


def do_clean():
    print('Cleaning up...')
    cmd('make -C core clean')
    cmd('rm -f bin/bessd')
    cmd('make -C core/kmod clean')
    cmd('rm -rf pybess/*_pb2.py')


def do_dist_clean():
    do_clean()
    print('Removing 3rd-party libraries...')
    cmd('rm -rf %s' % (DPDK_DIR))


def print_usage(parser):
    parser.print_help(file=sys.stderr)
    sys.exit(2)


def update_benchmark_path(path):
    print('Specified benchmark path %s' % path)
    cxx_flags.extend(['-I%s/include' % (path)])
    ld_flags.extend(['-L%s/lib' % (path)])


def main():
    os.chdir(BESS_DIR)
    parser = argparse.ArgumentParser(description='Build BESS')
    cmds = {
        'all': build_all,
        'download_dpdk': download_dpdk,
        'dpdk': build_dpdk,
        'bess': build_bess,
        'kmod': build_kmod,
        'clean': do_clean,
        'dist_clean': do_dist_clean,
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
    parser.add_argument(
        '--with-benchmark',
        dest='benchmark_path',
        nargs=1,
        help='Location of benchmark library')
    args = parser.parse_args()

    if args.benchmark_path:
        update_benchmark_path(args.benchmark_path[0])

    cmds[args.action]()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit('\nInterrupted')
