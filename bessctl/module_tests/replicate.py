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


class BessReplicateTest(BessModuleTestCase):

    def test_run_replicate4(self):
        rep4 = Replicate(gates=[0, 1, 2, 3])
        self.run_for(rep4, [0], 3)

    def test_run_replicate10(self):
        rep10 = Replicate(gates=[0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
        self.run_for(rep10, [0], 3)

    def test_run_replicate1(self):
        rep1 = Replicate(gates=[0])
        self.run_for(rep1, [0], 3)

    def test_replicate(self):
        rep3 = Replicate(gates=[0, 1, 2])
        pkt_in = get_tcp_packet(sip='22.22.22.22', dip='22.22.22.22')

        pkt_outs = self.run_module(rep3, 0, [pkt_in], [0, 1, 2])

        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_in)

        self.assertEquals(len(pkt_outs[1]), 1)
        self.assertSamePackets(pkt_outs[1][0], pkt_in)

        self.assertEquals(len(pkt_outs[2]), 1)
        self.assertSamePackets(pkt_outs[2][0], pkt_in)

suite = unittest.TestLoader().loadTestsFromTestCase(BessReplicateTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
