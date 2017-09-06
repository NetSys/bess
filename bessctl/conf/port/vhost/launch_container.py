#!/usr/bin/env python

# Copyright (c) 2017, Nefeli Networks, Inc.
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

import os
import sys
import subprocess
import time
import shlex

# How many cores we reserve for vSwitches?
# If set to 2, Containers will run on core 2, 3, 4, ..., skipping core 0-1.
VM_START_CPU = int(os.getenv('VM_START_CPU', '1'))
VM_MEM_SOCKET = int(os.getenv('VM_MEM_SOCKET', '0'))

HUGEPAGES_PATH = os.getenv('HUGEPAGES_PATH', '/dev/hugepages')

# "io retry" gives better throughput as the VM will not touch the packet
# payload, but it is somewhat unrealistic...
FWD_MODE = os.getenv('FWD_MODE', 'macswap retry')

# per container configuration
# NUM_CPUS cores are used for forwarding
NUM_VCPUS = int(os.getenv('VM_VCPUS', '2'))
NUM_VPORTS = int(os.getenv('BESS_PORTS', '2'))
NUM_QUEUES = int(os.getenv('BESS_QUEUES', '1'))

VERBOSE = int(os.getenv('VERBOSE', '0'))

SOCKDIR = '/tmp/bessd'
IMAGE = 'nefelinetworks/bess_build'
CONTAINER_NAME = 'nefeli_bessd'


def launch(cid):
    print('Running container {} as a forwarder'.format(cid))
    first_cpu = VM_START_CPU + cid * NUM_VCPUS
    last_cpu = first_cpu + NUM_VCPUS - 1
    eal_opts = '--file-prefix=vnf{} --no-pci -m 256 -l 0,{}-{}'.format(
        cid, first_cpu, last_cpu)

    for port_id in range(NUM_VPORTS):
        sockpath = '{}/vhost_user{}_{}.sock'.format(SOCKDIR, cid, port_id)
        eal_opts += ' --vdev=virtio_user{},path={}'.format(port_id, sockpath)

    testpmd_opts = '-i --burst=64 --txd=1024 --rxd=1024 --disable-hw-vlan ' \
        '--txqflags=0xf01 --txq={q} --rxq={q} --nb-cores={cores}'.format(
            q=NUM_QUEUES, cores=NUM_VCPUS)

    if subprocess.check_output(['numactl', '-H']).find(' 1 nodes') >= 0:
        cmd = ''
    else:
        cmd = 'numactl -m %d ' % VM_MEM_SOCKET

    cmd += 'docker run -i --rm --name {name} -v {huge}:{huge} -v {sock}:{sock} ' \
           '{image} {cmd} {eal_options} -- {testpmd_options}'.format(
               name=CONTAINER_NAME + str(cid), huge=HUGEPAGES_PATH, sock=SOCKDIR,
            image=IMAGE, cmd='/build/dpdk-17.05/build/app/testpmd',
            eal_options=eal_opts, testpmd_options=testpmd_opts)

    if VERBOSE:
        out = None  # to screen
        print(cmd)
    else:
        out = subprocess.PIPE

    proc = subprocess.Popen(shlex.split(cmd), stdin=subprocess.PIPE,
                            stdout=out, stderr=subprocess.STDOUT)
    proc.stdin.write('set fwd {}\n'.format(FWD_MODE))
    proc.stdin.write('start\n')
    return proc


def kill(cid):
    print('Terminating container {} '.format(cid))

    cmd = 'docker kill {name}'.format(name=CONTAINER_NAME + str(cid))

    if VERBOSE:
        print(cmd)

    try:
        proc = subprocess.check_call(shlex.split(cmd), stdout=subprocess.PIPE)
    except subprocess.CalledProcessError:
        pass


def main(argv):
    if len(argv) != 2:
        print('Usage: %s <# of containers to launch>' %
              argv[0], file=sys.stderr)
        return 2

    num_containers = int(argv[1])

    try:
        procs = [launch(i) for i in range(num_containers)]

        print('Press Ctrl+C to terminate all containers')
        while True:
            time.sleep(100)
    except KeyboardInterrupt:
        pass
    finally:
        for cid in range(num_containers):
            kill(cid)

    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
