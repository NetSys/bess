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

import sys
from test_utils import *


class BessVlanTest(BessModuleTestCase):

    def test_run_vlan(self):
        self.run_for(VLANSplit(), [0], 3)
        self.assertBessAlive()

    def _vlan_output_test(self, vids, double_tag=False, default_gate=0):
        def gen_vlan_packet(vid):
            eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
            vlan1 = scapy.Dot1AD(vlan=vid)
            vlan2 = scapy.Dot1Q(vlan=vid)
            ip = scapy.IP()
            udp = scapy.UDP(sport=10001, dport=10002)
            payload = 'truculence'
            single_tag = eth / vlan2 / ip / udp / payload
            double_tag = eth / vlan1 / vlan2 / ip / udp / payload
            return single_tag, double_tag

        def gen_untagged_packet():
            eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
            ip = scapy.IP()
            udp = scapy.UDP(sport=10001, dport=10002)
            payload = 'truculence'
            pkt = eth / ip / udp / payload
            return pkt

        expected = []

        vlan = VLANSplit()

        for vid in vids:
            p, pp = gen_vlan_packet(vid)
            q = gen_untagged_packet()
            if vid >= 0:
                if not double_tag:
                    pkt_outs = self.run_module(vlan, 0, [p], [vid])
                    self.assertEquals(len(pkt_outs[vid]), 1)
                    self.assertSamePackets(pkt_outs[vid][0], q)

                else:
                    pkt_outs = self.run_module(vlan, 0, [pp], [vid])
                    self.assertEquals(len(pkt_outs[vid]), 1)
                    self.assertSamePackets(pkt_outs[vid][0], p)

            else:
                pkt_outs = self.run_module(vlan, 0, [q], [default_gate])
                self.assertEquals(len(pkt_outs[default_gate]), 1)
                self.assertSamePackets(pkt_outs[default_gate][0], q)

    @unittest.skipUnless(hasattr(scapy, 'Dot1AD'), "this scapy lacks Dot1AD")
    def test_vlan_single_tag(self):
        self._vlan_output_test([1, 17, -1, 29, 10, 13, 7])

    @unittest.skipUnless(hasattr(scapy, 'Dot1AD'), "this scapy lacks Dot1AD")
    def test_vlan_double_tag(self):
        self._vlan_output_test([1, 17, -1, 29, 10, 13, 7], True)

suite = unittest.TestLoader().loadTestsFromTestCase(BessVlanTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
