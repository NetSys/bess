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


class BessAclTest(BessModuleTestCase):

    def test_run_acl_simple(self):
        fw = ACL(rules=[{'src_ip': '172.12.0.0/16', 'drop': False}])
        self.run_for(fw, [0], 3)
        self.assertBessAlive()

    def test_run_acl_3rules(self):
        fw = ACL(rules=[{'src_ip': '172.12.0.0/16',
                         'drop': False},
                        {'dst_ip': '192.168.32.4/32',
                         'dst_port': 4455,
                         'src_ip': '134.54.33.2/32',
                         'drop': False},
                        {'src_ip': '133.133.133.0/24',
                         'src_port': 43,
                         'dst_ip': '96.96.96.155/32',
                         'dst_port': 9,
                         'drop': False}])
        self.run_for(fw, [0], 3)
        self.assertBessAlive()

    def test_run_acl_edgecase(self):
        fw = ACL(rules=[{'src_ip': '0.0.0.0/0', 'drop': False}])
        self.run_for(fw, [0], 3)
        self.assertBessAlive()

    def test_acl_simple(self):
        fw = ACL(rules=[{'src_ip': '0.0.0.0/0', 'drop': False}])
                 # module to test
        pkt_in = get_tcp_packet(sip='22.22.22.22', dip='22.22.22.22')

        pkt_outs = self.run_module(fw, 0, [pkt_in], [0])

        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_in)

    def tests_acl_back2back(self):
        fw = ACL(rules=[{'src_ip': '96.0.0.0/8', 'drop': False}])
        pkt_in1 = get_tcp_packet(sip='22.22.22.22', dip='22.22.22.22')
        pkt_in2 = get_tcp_packet(sip='96.22.22.22', dip='22.22.22.22')

        pkt_outs = self.run_module(fw, 0, [pkt_in1], [0])
        self.assertEquals(len(pkt_outs[0]), 0)

        pkt_outs = self.run_module(fw, 0, [pkt_in2], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_in2)

    def test_run_acl_custom(self):
        fw = ACL(rules=[{'src_ip': '172.12.0.0/16',
                         'drop': False},
                        {'dst_ip': '192.168.32.4/32',
                         'dst_port': 4455,
                         'src_ip': '134.54.33.2/32',
                         'drop': False},
                        {'src_ip': '133.133.133.0/24',
                         'src_port': 43,
                         'dst_ip': '96.96.96.155/32',
                         'dst_port': 9,
                         'drop': False}])

        pkt_udp = get_udp_packet(sip='172.12.0.3', dip='127.12.0.4')
        pkt_tcp = get_tcp_packet(sip='192.168.32.4', dip='1.2.3.4')
        rwtemp = [bytes(pkt_udp), bytes(pkt_tcp)]

        Source() -> Rewrite(templates=rwtemp) -> fw -> Sink()

        bess.resume_all()
        time.sleep(3)
        bess.pause_all()

        self.assertBessAlive()

suite = unittest.TestLoader().loadTestsFromTestCase(BessAclTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
