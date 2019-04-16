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

from test_utils import *


class BessNatTest(BessModuleTestCase):
    # Test the packet mangling features with a single rule

    def _test_l4(self, module, l4_orig, ruleaddr):

        def _swap_l4(l4):
            ret = l4.copy()
            if type(l4) == scapy.UDP or type(l4) == scapy.TCP:
                ret.sport = l4.dport
                ret.dport = l4.sport
            return ret

        # There are 4 packets in this test:
        # 1. orig, 2. natted, 3. reply, 4. unnatted
        #
        # We're acting as 's0' and 's1' to test 'NAT'
        #
        # +----+    pkt_orig   +-------+    pkt_natted   +----+
        # |    |-------------->|       |---------------->|    |
        # | s0 |               |  NAT  |                 | s1 |
        # |    |<--------------|       |<----------------|    |
        # +----+  pkt_unnatted +-------+    pkt_repl     +----+

        eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
        ip_orig = scapy.IP(src='172.16.0.2', dst='8.8.8.8')
        ip_natted = scapy.IP(src=ruleaddr, dst='8.8.8.8')
        ip_reply = scapy.IP(src='8.8.8.8', dst=ruleaddr)
        ip_unnatted = scapy.IP(src='8.8.8.8', dst='172.16.0.2')
        l7 = 'helloworld'

        pkt_orig = eth / ip_orig / l4_orig / l7

        pkt_outs = self.run_module(module, 0, [pkt_orig], [0, 1])
        self.assertEquals(len(pkt_outs[1]), 1)
        pkt_natted = pkt_outs[1][0]

        # The NAT module can choose an arbitrary source port/id.
        # We cannot test it, we have to read from the output.
        l4_natted = l4_orig.copy()
        if type(l4_natted) == scapy.ICMP:
            l4_natted.id = pkt_natted[scapy.ICMP].id
        elif type(l4_natted) == scapy.UDP:
            l4_natted.sport = pkt_natted[scapy.UDP].sport
        elif type(l4_natted) == scapy.TCP:
            l4_natted.sport = pkt_natted[scapy.TCP].sport
        self.assertSamePackets(eth / ip_natted / l4_natted / l7, pkt_natted)

        l4_reply = _swap_l4(l4_natted)
        pkt_reply = eth / ip_reply / l4_reply / l7

        pkt_outs = self.run_module(module, 1, [pkt_reply], [0, 1])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(eth / ip_unnatted / _swap_l4(l4_orig) / l7,
                               pkt_outs[0][0])

    def test_nat_udp(self):
        nat_config = [{'ext_addr': '192.168.1.1'}]
        nat = NAT(ext_addrs=nat_config)
        self._test_l4(nat, scapy.UDP(sport=56797, dport=53), '192.168.1.1')

    def test_nat_udp_with_zero_cksum(self):
        nat_config = [{'ext_addr': '192.168.1.1'}]
        nat = NAT(ext_addrs=nat_config)
        self._test_l4(
            nat, scapy.UDP(sport=56797, dport=53, chksum=0), '192.168.1.1')

    def test_nat_tcp(self):
        nat_config = [{'ext_addr': '192.168.1.1'}]
        nat = NAT(ext_addrs=nat_config)
        self._test_l4(nat, scapy.TCP(sport=52428, dport=80), '192.168.1.1')

    def test_nat_icmp(self):
        nat_config = [{'ext_addr': '192.168.1.1'}]
        nat = NAT(ext_addrs=nat_config)
        self._test_l4(nat, scapy.ICMP(), '192.168.1.1')

    def test_nat_selfconfig(self):
        # Send initial conf unsorted, see that it comes back sorted
        # (note that this is a bit different from other modules
        # where argument order often matters).
        iconf = {'ext_addrs': [{'ext_addr': '192.168.1.1',
                                'port_ranges': [{'begin': 1, 'end': 1024},
                                                {'begin': 1025, 'end': 65535}]}]}
        nat = NAT(**iconf)
        arg = pb_conv.protobuf_to_dict(nat.get_initial_arg())
        expect_config = {}
        cur_config = pb_conv.protobuf_to_dict(nat.get_runtime_config())
        print("arg ", arg)
        print("iconf", iconf)
        print("cur_config", cur_config)
        print("expected_config", expect_config)
        assert arg == iconf and cur_config == expect_config


suite = unittest.TestLoader().loadTestsFromTestCase(BessNatTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
