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


class BessEthernetcapTest(BessModuleTestCase):

    '''
    def test_run_etherencap(self):
        metadata = SetMetadata(attrs=[{"name": "ether_src", "size": 6, "value_bin": b'\xde\xad\xbe\xef\x12\x34'},
            {"name": "ether_dst", "size": 6, "value_bin": b'\x12\x34\xde\xad\xbe\xef'},
            {"name" : "ether_type", "size": 2, "value_bin": b'\x08\x00'}])

        pkt_udp = get_udp_packet(sip = "172.12.0.3", dip = "172.12.0.4")
        pkt_tcp = get_tcp_packet(sip = "192.168.32.4", dip = "1.2.3.4")
        rwtemp = [bytes(pkt_udp), bytes(pkt_tcp)]

        Source() -> Rewrite(templates=rwtemp) -> metadata -> EtherEncap() -> Sink()

        bess.resume_all()
        time.sleep(3)
        bess.pause_all()

        self.assertBessAlive()
    '''
    # See if it correctly wraps IP packet in Ether header

    def test_metadata_outputtest_fixedvalues(self):
        eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
        ip = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
        udp = scapy.UDP(sport=10001, dport=10002)
        payload = 'helloworld'

        pkt_in = ip / udp / payload
        pkt_expected_out = eth / ip / udp / payload

        encap2 = EtherEncap()
        metadata = SetMetadata(
            attrs=[
                {"name": "ether_src", "size": 6,
                    "value_bin": b'\xde\xad\xbe\xef\x12\x34'},
                              {"name": "ether_dst", "size": 6,
                                  "value_bin": b'\x12\x34\xde\xad\xbe\xef'},
                {"name": "ether_type", "size": 2, "value_bin": b'\x08\x00'}])
        metadata -> encap2

        pkt_outs = self.run_pipeline(metadata, encap2, 0, [pkt_in], [0])
        self.assertEquals(len(pkt_outs[0]), 1)
        self.assertSamePackets(pkt_outs[0][0], pkt_expected_out)

suite = unittest.TestLoader().loadTestsFromTestCase(BessEthernetcapTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
