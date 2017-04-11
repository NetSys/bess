#!/usr/bin/env python

import os
import shlex
import subprocess
import sys
import time

TARGET_REPO = 'nefelinetworks/bess_build'

imgs = {'trusty64': {'arch': 'x86_64', 'base': 'ubuntu:trusty',
                     'tag_suffix': ''},
        'trusty32': {'arch': 'i386', 'base': 'ioft/i386-ubuntu:trusty',
                     'tag_suffix': '_32'},}

def print_usage(prog):
    print('Usage - {} [{}]'.format(prog, '|'.join(imgs.keys())))

def run_cmd(cmd, shell=False):
    if shell:
        subprocess.check_call(cmd, shell=True)
    else:
        subprocess.check_call(shlex.split(cmd))

def build(env):
    arch = imgs[env]['arch']
    base = imgs[env]['base']
    tag_suffix = imgs[env]['tag_suffix']
    bess_dpdk_branch = os.getenv('BESS_DPDK_BRANCH', 'master')
    version = time.strftime('%y%m%d')

    run_cmd('m4 -DBASE_IMAGE={} Dockerfile.m4 > Dockerfile'.format(base),
            shell=True)
    run_cmd('docker build --build-arg BESS_DPDK_BRANCH={branch} ' \
            '--build-arg DPDK_ARCH={arch} ' \
            '-t {target}:latest{suffix} -t {target}:{version}{suffix} ' \
            '.'.format(branch=bess_dpdk_branch, arch=arch, target=TARGET_REPO,
                       version=version, suffix=tag_suffix))

    print('Build succeeded: {}:{}{}'.format(TARGET_REPO, version, tag_suffix))
    print('Build succeeded: {}:latest{}'.format(TARGET_REPO, tag_suffix))

    return version, tag_suffix

def push(version, tag_suffix):
    run_cmd('docker login')
    run_cmd('docker push {}:latest{}'.format(TARGET_REPO, tag_suffix))
    run_cmd('docker push {}:{}{}'.format(TARGET_REPO, version, tag_suffix))

def main(argv):
    if len(argv) != 2 or argv[1] not in imgs.keys():
        print_usage(argv[0])
        return 2

    version, tag_suffix = build(argv[1])

    try:
        prompt = raw_input  # Python 2
    except NameError:
        prompt = input      # Python 3

    if prompt('Do you wish to push the image? [y/N] ').lower() in ['y', 'yes']:
        push(version, tag_suffix)
    else:
        print('The image was not pushed')

    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
