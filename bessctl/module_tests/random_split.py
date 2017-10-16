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


class BessRandomSplitTest(BessModuleTestCase):

    def test_dropnone(self):
        drop0 = RandomSplit(drop_rate=0, gates=[0])
        pkt_in = get_udp_packet()
        pkt_outs = self.run_module(drop0, 0, [pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_in)

    def test_dropall(self):
        drop0 = RandomSplit(drop_rate=1, gates=[0])
        pkt_in = get_udp_packet()
        pkt_outs = self.run_module(drop0, 0, [pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 0)

    def _drop_with_rate(self, rate):

        def _equal_with_noise(a, b, threshold):
            return abs((a - b)) <= threshold

        pktftm = [
            bytes(get_udp_packet()),
            bytes(get_tcp_packet())]

        ma = Measure()
        mb = Measure()

        Source() -> \
            ma -> \
            Rewrite(templates=pktftm) -> \
            RandomSplit(drop_rate=rate, gates=[0]) -> \
            mb -> \
            Sink()

        bess.resume_all()
        time.sleep(1)
        bess.pause_all()

        # Measure the ratio of packets dropped
        ratio = float(mb.get_summary().packets) / ma.get_summary().packets
        assert _equal_with_noise(ratio, 1 - rate, 0.05)

    def test_droprate_1(self):
        self._drop_with_rate(0.3)

    def test_droprate_2(self):
        self._drop_with_rate(0.5)

    def test_droprate_3(self):
        self._drop_with_rate(0.75)

    def test_droprate_4(self):
        self._drop_with_rate(0.9)

suite = unittest.TestLoader().loadTestsFromTestCase(BessRandomSplitTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
