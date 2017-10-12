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

filters = [
    "tcp src port 92",
        "len <= 1000",
        "ether proto 0x800",
        "ip proto 47 or ip6 proto 47",
        "ip host 22.22.22.22"
]


class BessBpfTest(BessModuleTestCase):

    def test_run_bpf_simple(self):
        bpf = BPF()
        filter0 = {"priority": 0, "filter": filters[0], "gate": 1}
        bpf.add(filters=[filter0])
        self.run_for(bpf, [0], 3)
        self.assertBessAlive()

    def test_run_bpf_complex(self):
        bpf = BPF()
        for i, exp in enumerate(filters):
            bpf.add(filters=[{"priority": i, "filter": exp, "gate": i}])
        self.run_for(bpf, [0], 3)
        self.assertBessAlive()

    # Test basic output/steering with single rule
    def test_bpf_single_rule(self):
        bpf = BPF()
        filter0 = {"priority": 0, "filter": filters[0], "gate": 1}
        bpf.add(filters=[filter0])

        pkt1 = get_udp_packet(sip='12.34.56.78', dip='12.34.56.78')
        pkt2 = get_tcp_packet(sip='12.34.56.78', dip='12.34.56.78',
                              sport=92)

        pkt_outs = self.run_module(bpf, 0, [pkt1], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt1)

        pkt_outs = self.run_module(bpf, 0, [pkt2], [1])
        self.assertEquals(len(pkt_outs[1]), 1)
        self.assertSamePackets(pkt_outs[1][0], pkt2)

    # Test multiple rules with priorities
    def test_bpf_multiple_rules(self):
        bpf = BPF()
        bpf.add(filters=[{"priority": 2, "filter": filters[0], "gate": 1}])
        bpf.add(filters=[{"priority": 1, "filter": filters[4], "gate": 2}])

        pkt1 = get_udp_packet(sip='22.22.22.22', dip='12.34.56.78',
                              sport=700)

        pkt2 = get_tcp_packet(sip='12.34.56.78', dip='22.22.22.22',
                              sport=92)

        pkt3 = get_tcp_packet(sip='12.34.56.78', dip='12.34.56.78',
                              sport=700)

        pkt_outs = self.run_module(bpf, 0, [pkt3], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt3)

        pkt_outs = self.run_module(bpf, 0, [pkt2], [1])
        self.assertEquals(len(pkt_outs[1]), 1)
        self.assertSamePackets(pkt_outs[1][0], pkt2)

        pkt_outs = self.run_module(bpf, 0, [pkt1], [2])
        self.assertEquals(len(pkt_outs[2]), 1)
        self.assertSamePackets(pkt_outs[2][0], pkt1)

suite = unittest.TestLoader().loadTestsFromTestCase(BessBpfTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
