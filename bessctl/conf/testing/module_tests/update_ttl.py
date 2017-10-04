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


class BessUpdateTTLTest(BessModuleTestCase):

    def test_run_update_ttl(self):
        uttl = UpdateTTL()
        self.run_for(uttl, [0], 3)

    def test_decrement(self):
        uttl = UpdateTTL()

        pkt_in = get_tcp_packet()
        pkt_in[scapy.IP].ttl = 2
        pkt_expected_out = scapy.Packet.copy(pkt_in)
        pkt_expected_out[scapy.IP].ttl = 1

        pkt_outs = self.run_module(uttl, 0, [pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_expected_out)

    def test_drop(self):
        # Drop test
        uttl = UpdateTTL()

        pkt_in = get_tcp_packet()
        pkt_in[scapy.IP].ttl = 2
        pkt_expected_out = scapy.Packet.copy(pkt_in)
        pkt_expected_out[scapy.IP].ttl = 1

        drop_pkt0 = get_tcp_packet()
        drop_pkt0[scapy.IP].ttl = 0
        drop_pkt1 = get_tcp_packet()
        drop_pkt1[scapy.IP].ttl = 1

        pkt_outs = self.run_module(uttl, 0, [drop_pkt0], [0])
        self.assertEquals(len(pkt_outs[0]), 0)

        pkt_outs = self.run_module(uttl, 0, [pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_expected_out)

        pkt_outs = self.run_module(uttl, 0, [drop_pkt1], [0])
        self.assertEquals(len(pkt_outs[0]), 0)

suite = unittest.TestLoader().loadTestsFromTestCase(BessUpdateTTLTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
