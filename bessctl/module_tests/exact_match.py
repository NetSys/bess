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

import socket
from test_utils import *
from pybess import protobuf_to_dict as pb_conv


class BessExactMatchTest(BessModuleTestCase):

    def test_run_exactmatch(self):
        # random fields, I have no idea what these are
        em = ExactMatch(fields=[{'offset': 23, 'num_bytes': 1},
                                {'offset': 2, 'num_bytes': 2},
                                {'offset': 29, 'num_bytes': 1}])
        em.add(fields=[{'value_bin': b'\xff'}, {'value_bin': b'\x23\xba'},
                       {'value_bin': b'\x34'}], gate=0)
        em.add(fields=[{'value_bin': b'\xff'}, {'value_bin': b'\x34\xaa'},
                       {'value_bin': b'\x12'}], gate=1)
        em.add(fields=[{'value_bin': b'\xba'}, {'value_bin': b'\x33\xaa'},
                       {'value_bin': b'\x22'}], gate=2)
        em.add(fields=[{'value_bin': b'\xaa'}, {'value_bin': b'\x34\xba'},
                       {'value_bin': b'\x32'}], gate=3)
        em.add(fields=[{'value_bin': b'\x34'}, {'value_bin': b'\x34\x7a'},
                       {'value_bin': b'\x52'}], gate=4)
        em.add(fields=[{'value_bin': b'\x12'}, {'value_bin': b'\x34\x7a'},
                       {'value_bin': b'\x72'}], gate=5)

        self.run_for(em, [0], 3)
        self.assertBessAlive()

    # Output test over fields -- just make sure packets go out right ports
    def test_exactmatch(self):
        # exact match for ip src and dst
        em = ExactMatch(fields=[{'offset': 26, 'num_bytes': 4},
                                {'offset': 30, 'num_bytes': 4}])
        em.add(fields=[{'value_bin': socket.inet_aton('65.43.21.0')},
                       {'value_bin': socket.inet_aton('12.34.56.78')}], gate=1)
        em.add(fields=[{'value_bin': socket.inet_aton('0.12.34.56')},
                       {'value_bin': socket.inet_aton('12.34.56.78')}], gate=2)
        em.set_default_gate(gate=3)

        pkt1 = get_tcp_packet(sip='65.43.21.0', dip='12.34.56.78')
        pkt2 = get_tcp_packet(sip='0.12.34.56', dip='12.34.56.78')
        pkt_nomatch = get_tcp_packet(sip='0.12.33.56', dip='12.34.56.78')

        pkt_outs = self.run_module(em, 0, [], [0])
        self.assertEquals(len(pkt_outs[0]), 0)

        pkt_outs = self.run_module(em, 0, [pkt1], [0, 1, 2, 3])
        self.assertEquals(len(pkt_outs[1]), 1)
        self.assertSamePackets(pkt_outs[1][0], pkt1)

        pkt_outs = self.run_module(em, 0, [pkt2], [0, 1, 2, 3])
        self.assertEquals(len(pkt_outs[2]), 1)
        self.assertSamePackets(pkt_outs[2][0], pkt2)

        pkt_outs = self.run_module(em, 0, [pkt_nomatch], [0, 1, 2, 3])
        self.assertEquals(len(pkt_outs[3]), 1)
        self.assertSamePackets(pkt_outs[3][0], pkt_nomatch)

    def test_exactmatch_with_metadata(self):
        # One exact match field
        em = ExactMatch(
            fields=[{"attr_name": "sangjin", "num_bytes": 2}], masks=[{"value_bin": b'\xff\xff'}])
        em.add(fields=[{'value_bin': b'\x88\x80'}], gate=1)
        em.add(fields=[{'value_bin': b'\x77\x70'}], gate=2)
        em.set_default_gate(gate=0)

        # Only need one test packet
        eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
        ip = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
        udp = scapy.UDP(sport=10001, dport=10002)
        payload = 'helloworld'
        test_packet_in = ip / udp / payload

        # Three kinds of metadata tags
        metadata = []
        metadata.append(SetMetadata(
            attrs=[{"name": "sangjin", "size": 2, "value_bin": b'\x77\x90'}]))
        metadata.append(SetMetadata(
            attrs=[{"name": "sangjin", "size": 2, "value_bin": b'\x88\x80'}]))
        metadata.append(SetMetadata(
            attrs=[{"name": "sangjin", "size": 2, "value_bin": b'\x77\x70'}]))

        # And a merge module
        merger = Merge()
        merger -> em

        for i in range(3):
            metadata[i] -> merger

        for i in range(3):
            pkt_outs = self.run_pipeline(
                metadata[i], em, 0, [test_packet_in], range(3))
            self.assertEquals(len(pkt_outs[i]), 1)
            self.assertSamePackets(pkt_outs[i][0], test_packet_in)

    def test_exactmatch_selfconfig(self):
        "make sure get_initial_arg and [gs]et_runtime_config work"
        iconf = {
            'fields': [{'attr_name': 'babylon5', 'num_bytes': 2},
                       {'offset': 10, 'num_bytes': 1}],
            'masks': [{'value_bin': b'\xff\xf0'}, {'value_bin': b'\x7f'}]
        }
        em = ExactMatch(**iconf)
        # workers are all paused, we never run them here
        em.add(fields=[{'value_bin': b'\x88\x80'}, {'value_bin': b'\x03'}], gate=1)
        em.add(fields=[{'value_bin': b'\x77\x70'}, {'value_bin': b'\x05'}], gate=2)
        em.set_default_gate(gate=3)
        # Delivered config is sorted by gate first, then field values
        # if gates are identical.  Keep expected config sorted here.
        expect_config = {
            'default_gate': 3,
            'rules': [
                {'fields': [{'value_bin': b'\x88\x80'}, {'value_bin': b'\x03'}],
                 'gate': 1},
                {'fields': [{'value_bin': b'\x77\x70'}, {'value_bin': b'\x05'}],
                 'gate': 2 },
            ]
        }
        # or: expect_config['rules'].sort(lambda i: (i['gate'], j['value_bin'] for j in i['fields']))
        arg = pb_conv.protobuf_to_dict(em.get_initial_arg())
        cur_config = pb_conv.protobuf_to_dict(em.get_runtime_config())
        #import pprint
        #def pp2(*args):
        #    for a, b in zip(*[iter(args)] * 2):
        #        print('{}:'.format(a))
        #        pprint.pprint(b, indent=4)
        #pp2('iconf:', iconf, 'arg:', arg,
        #    '\nmut state:', cur_config, 'expecting:', expect_config)
        assert arg == iconf and cur_config == expect_config

suite = unittest.TestLoader().loadTestsFromTestCase(BessExactMatchTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
