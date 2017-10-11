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


class BessDecapTest(BessModuleTestCase):

    def test_run_decap_0bytes(self):
        gd = GenericDecap(bytes=0)
        self.run_for(gd, [0], 3)
        self.assertBessAlive()

    def test_run_decap_morebytes(self):
        gd = GenericDecap(bytes=23)
        self.run_for(gd, [0], 3)
        self.assertBessAlive()

    # test strip off ether
    def test_decap(self):
        gd = GenericDecap(bytes=14)

        eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
        ip = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
        udp = scapy.UDP(sport=10001, dport=10002)
        payload = 'helloworldhelloworldhelloworld'

        pkt_in = eth / ip / udp / payload
        pkt_expected_out = ip / udp / payload

        pkt_outs = self.run_module(gd, 0, [pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_expected_out)

suite = unittest.TestLoader().loadTestsFromTestCase(BessDecapTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
