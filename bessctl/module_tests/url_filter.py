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

import socket
import sys
from test_utils import *
from pybess import protobuf_to_dict as pb_conv


class BessUrlFilterTest(BessModuleTestCase):

    def test_run_urlfilter(self):
        uf = UrlFilter()
        uf.add(blacklist=[
            {'host': 'www.foo.com', 'path': '/foo'},
            {'host': 'www.bar.com', 'path': '/bar'}
        ])
        self.run_for(uf, [0], 3)
        self.assertBessAlive()

    # Output test -- make sure packets go out right ports
    def test_urlfilter(self):
        uf = UrlFilter()
        uf.add(blacklist=[{'host': 'www.blacklisted.com', 'path': '/'}])

        # Eth: us to them; eth_swapped: them to us
        eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
        eth_swapped = scapy.Ether(src=eth.dst, dst=eth.src)
        # ip1 for "OK" request, ip2 for bad (gets 403) request
        ip1 = scapy.IP(src='192.168.0.1', dst='10.0.0.1')
        ip2 = scapy.IP(src='192.168.0.2', dst='10.0.0.2')
        # tcp for SYNs, tplus for packet following SYN
        tcp = scapy.TCP(sport=10001, dport=80, seq=12345)  # has syn
        tplus = tcp.copy()
        tplus.ack = 23456
        tplus.flags = 0
        tplus.seq += 1
        # t403 is for faked 403: like tplus, but with ports and
        # seq/ack swapped around, ACK set, and window slammed shut.
        t403 = tplus.copy()
        t403.sport, t403.dport = t403.dport, t403.sport
        t403.seq, t403.ack = tplus.ack, tplus.seq
        t403.flags = 'A'
        t403.window = 0
        # and of course, a "good" request vs a 403 "bad" request
        good_payload = 'GET / HTTP/1.1\r\nHost: www.google.com\r\n\r\n'
        bad_payload = 'GET / HTTP/1.1\r\nHost: www.blacklisted.com\r\n\r\n'
        error_403 = 'HTTP/1.1 403 Bad Forbidden\r\nConnection: Closed\r\n\r\n'
        syn_pkt1 = bytes(eth / ip1 / tcp)
        syn_pkt2 = bytes(eth / ip2 / tcp)
        good_pkt = bytes(eth / ip1 / tplus / good_payload)
        bad_pkt = bytes(eth / ip2 / tplus / bad_payload)
        err_pkt = bytes(eth_swapped / scapy.IP(src=ip2.dst, dst=ip2.src) /
                        t403 / error_403)

        pkt_outs = self.run_pipeline(src_module=uf, dst_module=uf,
                                     igate=0,
                                     input_pkts=[syn_pkt1, good_pkt,
                                                 syn_pkt2, bad_pkt],
                                     ogates=[0, 1])

        # We should receive 4 packets on gate 0:
        #  1) SYN to google.com
        #  2) GET to google.com
        #  3) SYN to blacklisted.com
        #  3) RST|ACK to blacklisted.com
        # and 2 packets on gate 1:
        #  1) 403 to 192.168.0.2
        #  2) RST to 192.168.0.2
        self.assertEquals(len(pkt_outs[0]), 4)
        self.assertSamePackets(pkt_outs[0][1], good_pkt)

        self.assertEquals(len(pkt_outs[1]), 2)
        self.assertSamePackets(pkt_outs[1][0], err_pkt)

    def test_urlfilter_selfconfig(self):
        iconf = {}
        uf = UrlFilter(**iconf)
        uf.add(blacklist=[
            {'host': 'www.bluelisted.com', 'path': '/b'},
            {'host': 'www.bluelisted.com', 'path': '/a'},
            {'host': 'www.blacklisted.com', 'path': '/'},
        ])
        # Delivered config is sorted by host, then path within host.
        # Blue sorts after black ('u' > 'a').
        expect_config = {'blacklist': [
            {'host': 'www.blacklisted.com', 'path': '/'},
            {'host': 'www.bluelisted.com', 'path': '/a'},
            {'host': 'www.bluelisted.com', 'path': '/b'},
        ]}
        arg = pb_conv.protobuf_to_dict(uf.get_initial_arg())
        cur_config = pb_conv.protobuf_to_dict(uf.get_runtime_config())
        # import pprint
        # def pp2(*args):
        #    for a, b in zip(*[iter(args)] * 2):
        #        print('{}:'.format(a))
        #        pprint.pprint(b, indent=4)
        # pp2('iconf:', iconf, 'arg:', arg,
        #    '\nmut state:', cur_config, 'expecting:', expect_config)
        assert arg == iconf and cur_config == expect_config

suite = unittest.TestLoader().loadTestsFromTestCase(BessUrlFilterTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
