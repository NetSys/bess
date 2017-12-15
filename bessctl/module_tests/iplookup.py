# Copyright (c) 2017  Tamas Levai <levait@tmit.bme.hu>
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


class BessIPLookupTest(BessModuleTestCase):

    def test_iplookup(self):
        ipl = IPLookup()
        pkts = [get_tcp_packet(sip='12.22.22.22', dip='22.22.22.22'),
                get_tcp_packet(sip='12.22.22.22', dip='32.22.22.22'),
                get_tcp_packet(sip='12.22.22.22', dip='42.22.22.22')]

        ipl.add(prefix='22.22.22.0', prefix_len=24, gate=0)
        ipl.add(prefix='32.22.22.0', prefix_len=24, gate=1)
        ipl.add(prefix='42.22.22.0', prefix_len=24, gate=1)

        ipl.delete(prefix='42.22.22.0', prefix_len=24)
        with self.assertRaises(bess.Error):
            ipl.delete(prefix='52.22.22.0', prefix_len=24)

        pkt_outs = self.run_module(ipl, 0, pkts, [0, 1])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertEquals(len(pkt_outs[1]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkts[0])
        self.assertSamePackets(pkt_outs[1][0], pkts[1])

    def test_prefix(self):
        ipl = IPLookup()
        with self.assertRaises(bess.Error):
            ipl.add(prefix='22.22.22.0', prefix_len=16, gate=0)


suite = unittest.TestLoader().loadTestsFromTestCase(BessIPLookupTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
