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

import codecs
import os
import random
import scapy.all as scapy
import shlex
import socket
import subprocess
import sys
import unittest
from time import gmtime, sleep, strftime

try:
    this_dir = os.path.dirname(os.path.realpath(__file__))
    bessctl = os.path.join(this_dir, 'bessctl')
    sys.path.insert(1, os.path.join(this_dir, '../../../'))
    from pybess.bess import *
except ImportError:
    print('Cannot import the API module (pybess)', file=sys.stderr)
    raise

SOCKET_PATH = '/tmp/bess_unix_'
SCRIPT_STARTTIME = strftime("%Y-%m-%d-%H-%M-%S", gmtime())


def get_root_tc(bess):
    # get tc information
    tcs = bess.list_tcs().classes_status
    root_tc = None
    for tc in tcs:
        if not getattr(tc, 'parent'):
            root_tc = getattr(tc, 'class')
            break

    return root_tc


def measure_tc_perf(bess, duration):
    root_tc = get_root_tc(bess)
    if not root_tc:
        raise Exception('Fail to find root tc')

    old = bess.get_tc_stats(root_tc.name)
    time.sleep(duration)
    new = bess.get_tc_stats(root_tc.name)

    sec_diff = new.timestamp - old.timestamp
    pps = (new.packets - old.packets) / sec_diff
    cps = (new.count - old.count) / sec_diff
    sys.stdout.write("pps: %12.f\tcounts/s: %12.f\n" % (pps, cps))


def run_cmd(cmd):
    args = shlex.split(cmd)
    print(cmd)
    try:
        subprocess.check_output(args, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
        raise CommandError(e.returncode, e.cmd, e.output)


def gen_unix_socket(bess, sockname, timeout_sec=3):
    """
    Create a socket that can send data to BESS and/or receive
    data from BESS, once it has a PortInc or PortOut or similar
    wrapped around it.

    The sockname argument is the name to be given to PortInc/PortOut.
    """
    # Regarding 'confirm_connect' = True here:
    #
    # The create_port() call runs a race: BESS spins off a thread
    # listen()ing on the abstract path '\0' + SOCKET_PATH + sockname.
    # When we connect() to it below, the thread eventually wakes up
    # and creates the in-BESS listener.
    socket_port = bess.create_port('UnixSocketPort', sockname,
                                   {
                                       'path': '@' + SOCKET_PATH + sockname,
                                       # 'min_rx_interval_ns': 50000,
                                       'confirm_connect': True,
                                   })
    s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    s.settimeout(timeout_sec)

    s.connect('\0' + SOCKET_PATH + sockname)
    # This connect() is in the middle of the race.  If we return
    # now, the in-BESS listener may not have actually set up the
    # connection.  We can make more bess.* calls, e.g., to create
    # a PortOut object and wire it to the listener.  Then we can
    # inject a packet into bess, have it flow through to the port,
    # and have it arrive at the unix_socket driver in BESS, all
    # before the connection really finishes.
    #
    # If all that happens, the unix_socket.cc driver will drop the
    # packet (and we'll count the drop).  If a test like
    # module_tests/ vlan.py (for VlanSplit()) expects the packet
    # to arrive, the test will fail.
    #
    # This scenario seems unlikely, and yet it's actually happening
    # in a VM running on a Mac host.  By setting confirm_connect,
    # we make the unix_socket port driver send a "yes\0" packet to
    # us once the connection finishes.  We read this here to guarantee
    # that the connection is live and it's OK to run the test.
    confirmed = s.recv(2048)
    if confirmed != b'yes\0':
        raise AssertionError('port not connected '
                             '({!r} != {!r})'.format(confirmed, b'yes\0'))

    return s


# generate random packet
def get_udp_packet(sip=None, dip=None, sport=None, dport=None, pkt_len=60):
    eth = scapy.Ether(src=scapy.RandMAC()._fix(),
                      dst=scapy.RandMAC()._fix())
    ip = scapy.IP(src=sip if sip else scapy.RandIP()._fix(),
                  dst=dip if dip else scapy.RandIP()._fix())
    udp = scapy.UDP(sport=sport if sport else random.randrange(pow(2, 16)),
                    dport=dport if dport else random.randrange(pow(2, 16)))
    header = eth / ip / udp
    payload = '0' * (pkt_len - len(header))
    return header / payload


def get_tcp_packet(sip=None, dip=None, sport=None, dport=None, pkt_len=60):
    eth = scapy.Ether(src=scapy.RandMAC()._fix(),
                      dst=scapy.RandMAC()._fix())
    ip = scapy.IP(src=sip if sip else scapy.RandIP()._fix(),
                  dst=dip if dip else scapy.RandIP()._fix())
    tcp = scapy.TCP(sport=sport if sport else random.randrange(pow(2, 16)),
                    dport=dport if dport else random.randrange(pow(2, 16)))
    header = eth / ip / tcp
    payload = '0' * (pkt_len - len(header))
    return header / payload


def pkt_str(pkt):
    if not pkt:
        return ''
    else:
        return codecs.encode(bytes(pkt), 'hex')


class BessModuleTestCase(unittest.TestCase):

    @staticmethod
    def assertSamePackets(pkt1, pkt2):
        if pkt_str(pkt1) != pkt_str(pkt2):
            raise AssertionError(
                '"%s" != "%s"' % (pkt_str(pkt1), pkt_str(pkt2)))

    @staticmethod
    def assertNotSamePackets(pkt1, pkt2):
        if pkt_str(pkt1) == pkt_str(pkt2):
            raise AssertionError(
                '"%s" == "%s"' % (pkt_str(pkt1), pkt_str(pkt2)))

    def assertBessAlive(self):
        try:
            self.bess.get_version()
        except BESS.APIError:
            raise AssertionError('Bess is not alive')

    def setUp(self):
        self.bess = BESS()

        try:
            self.bess.connect()
        except BESS.APIError:
            raise Exception('BESS is not running')

        self.bess.pause_all()
        self.bess.reset_all()

        self.sockets = {}
        self.input_ports = {}
        self.output_ports = {}

    def tearDown(self):
        for sock in self.sockets.values():
            sock.close()

        self.bess.pause_all()
        self.bess.reset_all()

    def run_for(self, module, igates, duration, pkt_update_fields=[]):
        self.bess.pause_all()

        fields = pkt_update_fields
        if len(fields) == 0:
            fields.append({'offset': 26, 'size': 4,
                           'min': 1, 'max': pow(2, 32) - 1})
            fields.append({'offset': 30, 'size': 4,
                           'min': 1, 'max': pow(2, 32) - 1})

        # source and associate sockets
        for igate in igates:
            src = self.bess.create_module('Source')
            random = self.bess.create_module('RandomUpdate', 'RandomUpdateArg',
                                             {'fields': fields})
            self.bess.connect_modules(src.name, random.name)
            self.bess.connect_modules(random.name, module.name, 0, igate)

        # running tests
        self.bess.resume_all()
        time.sleep(duration)
        self.bess.pause_all()

    def run_pipeline(self, src_module, dst_module, igate, input_pkts,
                     ogates, time_out=3, proto=scapy.Ether):
        """
        Runs a pipeline that injects data (input_pkts) at module
        src_module on input gate igate and collects it at module
        dst_module on output gates ogates.  Runs the pipeline for
        at most time_out seconds; stops sooner if and when all the
        input packets are processed.

        input_pkts may be a single bytestring, or a list of them.
        """
        if not isinstance(input_pkts, list):
            input_pkts = [input_pkts]

        self.bess.pause_all()

        # output ports and associate sockets
        for ogate in ogates:
            if ogate not in self.output_ports:
                sock_name = "soc_{}_{}".format(ogate,
                                               SCRIPT_STARTTIME)

                if ogate not in self.sockets:
                    sock = gen_unix_socket(self.bess, sock_name)
                    self.sockets[ogate] = sock

                po = self.bess.create_module('PortOut', 'po%d' % ogate,
                                             {'port': sock_name})
                self.output_ports[ogate] = po

            po = self.output_ports[ogate]
            self.bess.disconnect_modules(dst_module.name, ogate)
            self.bess.connect_modules(dst_module.name, po.name, ogate, 0)

        # input ports and associate sockets
        if igate not in self.input_ports:
            sock_name = "soc_{}_{}".format(igate,
                                           SCRIPT_STARTTIME)

            if igate not in self.sockets:
                sock = gen_unix_socket(self.bess, sock_name)
                self.sockets[igate] = sock

            pi = self.bess.create_module('PortInc', 'pi%d' % igate,
                                         {'port': sock_name})
            self.input_ports[igate] = pi

        pi = self.input_ports[igate]
        self.bess.disconnect_modules(pi.name)
        self.bess.connect_modules(pi.name, src_module.name, 0, igate)

        self.bess.resume_all()  # attach all orphan tasks into root
        self.bess.pause_all()

        root_tc = get_root_tc(self.bess)
        if not root_tc:
            raise Exception('Fail to find root tc')

        # Get number of packets processed inside bess.
        # Send our packets in, then wait for them to also
        # get processed.
        last = self.bess.get_tc_stats(root_tc.name)

        self.bess.resume_all()

        for pkt in input_pkts:
            self.sockets[igate].send(bytes(pkt))

        duration = 0
        while duration <= time_out:
            cur = self.bess.get_tc_stats(root_tc.name)
            if cur.packets - last.packets >= len(input_pkts):
                break
            time.sleep(0.1)
            duration += 0.1

        out_pkts = self._collect_output(ogates, proto)

        self.bess.pause_all()

        return out_pkts

    def _collect_output(self, ogates, proto):
        """
        Collects output on self.sockets[] per ogates.  Returns a
        dictionary indexed by ogate with a list of each packet
        wrapped by scapy.Ether.
        """
        def get_all_pkts(sock):
            """
            Reads all immediately-available packets.  Wrecks
            the current timeout/nonblock setting.
            """
            ret = []
            sock.settimeout(0.0)
            while True:
                try:
                    received_data = sock.recv(2048)
                    ret.append(proto(received_data))
                # NB: sock.settimeout(0.0) logically should
                # produce a socket.timeout, not a socket.error
                # with EAGAIN, but in fact we get the latter.
                except socket.error as e:
                    if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                        return ret
                    raise
                except socket.timeout:
                    return ret

        timeout = {}
        out_pkts = {}
        for ogate in ogates:
            timeout[ogate] = self.sockets[ogate].gettimeout()
        try:
            for ogate in ogates:
                out_pkts[ogate] = get_all_pkts(self.sockets[ogate])
            return out_pkts
        finally:
            for ogate in ogates:
                self.sockets[ogate].settimeout(timeout[ogate])

    def run_module(self, module, igate, input_pkts, ogates=range(16),
                   time_out=3, proto=scapy.Ether):
        return self.run_pipeline(module, module, igate, input_pkts, ogates,
                                 time_out=time_out, proto=proto)
