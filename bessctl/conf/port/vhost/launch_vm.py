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

from qmp import QEMUMonitorProtocol

# How many cores we reserve for vSwitches?
# If set to 2, VMs will run on core 2, 3, 4, ..., skipping core 0-1.
VM_START_CPU = int(os.getenv('VM_START_CPU', '1'))
VM_MEM_SOCKET = int(os.getenv('VM_MEM_SOCKET', '0'))

HUGEPAGES_PATH = os.getenv('HUGEPAGES_PATH', '/dev/hugepages')

# "io retry" gives better throughput as the VM will not touch the packet
# payload, but it is somewhat unrealistic...
FWD_MODE = os.getenv('FWD_MODE', 'macswap retry')

# per VM configuration
# NUM_VCPUS-1 cores are used for forwarding, leaving 1 core for mgmt purposes
NUM_VCPUS = int(os.getenv('VM_VCPUS', '3'))
NUM_VPORTS = int(os.getenv('BESS_PORTS', '2'))
NUM_QUEUES = int(os.getenv('BESS_QUEUES', '1'))

VERBOSE = int(os.getenv('VERBOSE', '0'))

qemu_cmd_template = \
    'kvm -snapshot -enable-kvm -cpu host -smp {cpus} -m 2048 ' \
        '-object memory-backend-file,id=ram,size=2048M,mem-path=%s,share=on ' \
        '-numa node,memdev=ram -mem-prealloc ' \
        '-qmp unix:/tmp/qmp{vm}.sock,server,nowait ' \
        '-device virtio-net-pci,netdev=mgmt,mac=52:54:00:12:34:56 ' \
        '-netdev user,id=mgmt,hostfwd=tcp:127.0.0.1:2200{vm}-:22 ' \
        '-vnc 127.0.0.1:{vm} -k en-us ' \
        'vm.qcow2' % HUGEPAGES_PATH

vhost_opt_template = \
    '-chardev socket,id=char{nic},path=/tmp/bessd/vhost_user{vm}_{nic}.sock ' \
        '-netdev type=vhost-user,id=vhu{nic},chardev=char{nic},queues={q} ' \
        '-device virtio-net-pci,netdev=vhu{nic},id=net{nic},mac=52:54:00:00:0{vm}:0{nic},bus=pci.0,addr=0x1{nic},csum=off,gso=off,guest_tso4=off,guest_tso6=off,guest_ecn=off,mrg_rxbuf=off,mq=on,vectors=18'

this_dir = os.path.dirname(os.path.realpath(__file__))
bess_dir = os.path.join(this_dir, '../../../../')

# Return the thread ID of each vcpu


def get_threads(path):
    srv = QEMUMonitorProtocol(path)
    srv.connect()

    def do_command(srv, cmd, **kwds):
        rsp = srv.cmd(cmd, kwds)
        if 'error' in rsp:
            raise Exception(rsp['error']['desc'])
        return rsp['return']

    rsp = do_command(srv, 'query-cpus')
    srv.close()

    ret = []
    for i, vcpu in enumerate(rsp):
        assert(vcpu['CPU'] == i)
        ret.append(vcpu['thread_id'])
    return ret


def scp(vm_id, localpath, remotepath):
    cmd = "scp -q -i vm.key -oStrictHostKeyChecking=no -P 2200{} -r {} vagrant@localhost:{}".format(
        vm_id, localpath, remotepath)
    subprocess.check_call(shlex.split(cmd))


def ssh_cmd(vm_id, cmd=''):
    ret = "ssh -q -i vm.key -oStrictHostKeyChecking=no -p 2200{vm} vagrant@localhost".format(
        vm=vm_id)
    if cmd:
        ret += " '{cmd}'".format(cmd=cmd)
    return ret


def launch(vm_id, num_nics, vhost_opts):
    assert(vm_id < 10)
    assert(num_nics < 10)

    if subprocess.check_output(shlex.split('numactl -H')).find(' 1 nodes') >= 0:
        cmd = ''
    else:
        cmd = 'numactl -m %d ' % VM_MEM_SOCKET

    cmd += qemu_cmd_template.format(vm=vm_id, cpus=NUM_VCPUS)
    for i in range(num_nics):
        cmd += ' ' + vhost_opts.format(vm=vm_id, nic=i, q=NUM_QUEUES)

    proc = subprocess.Popen(shlex.split(cmd))
    if VERBOSE:
        print(cmd)
    print(
        'VM{vm} is running with {nics} virtio ports:'.format(vm=vm_id, nics=num_nics))
    print('{cmd}'.format(cmd=ssh_cmd(vm_id)))
    print('gvncviewer localhost:{vm}'.format(vm=vm_id))
    time.sleep(5)

    # Pin vCPU [1, N) on dedicated physical CPUs
    # We don't care vCPU 0 as it will be scheduled on a non-isolated core.
    print('  vCPU 0 -> CPU (any non-isolated)')
    threads = get_threads('/tmp/qmp{vm}.sock'.format(vm=vm_id))
    for i in range(1, NUM_VCPUS):
        pcpu = VM_START_CPU + (NUM_VCPUS - 1) * vm_id + (i - 1)
        cmd = 'taskset -pc {pcpu} {pid}'.format(pcpu=pcpu, pid=threads[i])
        print('  vCPU {vcpu} -> CPU {pcpu}'.format(vcpu=i, pcpu=pcpu))
        subprocess.check_call(shlex.split(cmd), stdout=subprocess.PIPE)

    return proc


def run_forward(vm_id, num_nics):
    print('Running VM{vm} as a forwarder'.format(vm=vm_id))
    print('  Waiting for SSH to be available...', end='')
    sys.stdout.flush()
    cmd = ssh_cmd(vm_id, 'true')
    while subprocess.call(shlex.split(cmd)):
        time.sleep(1)
        print('.', end='')
        sys.stdout.flush()
    print('OK')

    print('  Setting up hugepages...')
    cmd = ssh_cmd(vm_id, 'sudo sysctl vm.nr_hugepages=512')
    subprocess.check_call(shlex.split(cmd), stdout=subprocess.PIPE)
    cmd = ssh_cmd(vm_id, 'sudo mkdir -p /mnt/huge')
    subprocess.check_call(shlex.split(cmd))
    cmd = ssh_cmd(vm_id, 'sudo mount -t hugetlbfs none /mnt/huge')
    subprocess.check_call(shlex.split(cmd))

    print('  Binding virtio NICs to DPDK')
    nics = ''
    for i in range(num_nics):
        nics += ' 00:1{nic}.0'.format(nic=i)

    scp(vm_id, os.path.join(bess_dir, 'bin/dpdk-devbind.py'), '')
    scp(vm_id, os.path.join(bess_dir, 'deps/dpdk-17.05/build/app/testpmd'), '')

    # virtio-pci devices should not be bound to any driver
    cmd = ssh_cmd(vm_id, 'sudo ./dpdk-devbind.py -u %s' % nics)
    subprocess.check_call(shlex.split(cmd))

    print('  Running testpmd...')
    eal_opt = '-c 0x{coremask:x} -n 4'.format(coremask=(1 << NUM_VCPUS) - 1)
    testpmd_opt = '-i --burst=64 --txd=1024 --rxd=1024 --txq={q} --rxq={q} --nb-cores={fwdcores} --disable-hw-vlan --txqflags=0xf01' \
        .format(fwdcores=NUM_VCPUS - 1, q=NUM_QUEUES)
    testpmd_cmd = 'sudo ./testpmd {} -- {}'.format(eal_opt, testpmd_opt)
    cmd = ssh_cmd(vm_id, '(echo -e "set fwd %s\nstart" && cat) | %s' %
                  (FWD_MODE, testpmd_cmd))
    subprocess.Popen(
        shlex.split(cmd), stdin=subprocess.PIPE, stdout=subprocess.PIPE)


def main(argv):
    if os.getuid() != 0:
        print('You must be root', file=sys.stderr)
        return 1

    if len(argv) != 2:
        print('Usage: %s <# of VMs to launch>' % argv[0], file=sys.stderr)
        return 2

    os.system('pkill -f qemu-system-x86_64 > /dev/null 2>&1')

    num_vms = int(argv[1])
    procs = []
    try:
        for i in range(num_vms):
            proc = launch(i, NUM_VPORTS, vhost_opt_template)
            procs.append(proc)

        for i in range(num_vms):
            run_forward(i, NUM_VPORTS)

        print('Press Ctrl+C to terminate all VMs')
        while True:
            time.sleep(100)
    except KeyboardInterrupt:
        pass
    finally:
        for proc in procs:
            print('Terminating VM (pid=%d)' % proc.pid)
            proc.terminate()
            proc.wait()

    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
