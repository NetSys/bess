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

# BESS default batch size
# TODO: Any way to receive the parameter from bess daemon?
BATCH_SIZE = 32


class BessBufferTest(BessModuleTestCase):

    def test_run_buffer(self):
        buf = Buffer()
        self.run_for(buf, [0], 3)
        self.assertBessAlive()

    def test_buffer(self):
        buf = Buffer()
        pkt1 = get_tcp_packet(sip='22.22.22.22', dip='22.22.22.22')
        pkt2 = get_tcp_packet(sip='32.22.22.22', dip='22.22.22.22')

        test_data = []

        # Should withhold data until it has a full batch to push through.
        for i in range(BATCH_SIZE - 1):
            test_data.append({'input_port': 0, 'input_packet': pkt1,
                              'output_port': 0, 'output_packet': None})

        test_data.append({'input_port': 0, 'input_packet': pkt2,
                          'output_port': 0, 'output_packet': pkt1})

        for i in range(BATCH_SIZE - 1):
            pkt_outs = self.run_module(buf, 0, [pkt1], [0])
            self.assertEquals(len(pkt_outs[0]), 0)

        pkt_outs = self.run_module(buf, 0, [pkt2], [0])
        self.assertEquals(len(pkt_outs[0]), BATCH_SIZE)
        self.assertSamePackets(pkt_outs[0][0], pkt1)

suite = unittest.TestLoader().loadTestsFromTestCase(BessBufferTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
