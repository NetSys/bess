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


class BessIpChecksumTest(BessModuleTestCase):

    def test_bypass(self):
        wmodule = IPChecksum()

        eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
        vlan = scapy.Dot1Q(vlan=6)
        ip_wrong = scapy.IP(
            src="1.2.3.4", dst="2.3.4.5", ttl=98, chksum=0x0000)
        ip_right = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
        udp = scapy.UDP(sport=10001, dport=10002)
        payload = 'helloworldhelloworldhelloworld'

        in_out = []

        eth_in = eth / ip_wrong / udp / payload
        eth_out = eth / ip_right / udp / payload
        self.assertNotSamePackets(eth_in, eth_out)

        vlan_in = eth / vlan / ip_wrong / udp / payload
        vlan_out = eth / vlan / ip_right / udp / payload
        self.assertNotSamePackets(vlan_in, vlan_out)

        pkt_outs = self.run_module(wmodule, 0, [eth_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], eth_out)

        pkt_outs = self.run_module(wmodule, 0, [vlan_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], vlan_out)

        # scapy-python3 doesn't have Dot1AD
        if hasattr(scapy, 'Dot1AD'):
            qinq = scapy.Dot1AD(vlan=5)

            qinq_in = eth / qinq / vlan / ip_wrong / udp / payload
            qinq_out = eth / qinq / vlan / ip_right / udp / payload
            self.assertNotSamePackets(qinq_in, qinq_out)

            pkt_outs = self.run_module(wmodule, 0, [qinq_in], [0])
            self.assertEquals(len(pkt_outs[0]), 1)
            self.assertSamePackets(pkt_outs[0][0], qinq_out)

suite = unittest.TestLoader().loadTestsFromTestCase(BessIpChecksumTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
