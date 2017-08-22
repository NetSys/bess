# Copyright (c) 2017, Nefeli Networks, Inc.
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

import scapy.all as scapy
import sys

if sys.version_info[0] != 2:
    print("python3-compatible scapy does not have Dot1AD. Skipping vlan test...")
else:
    ## CRASH TESTS ##
    CRASH_TEST_INPUTS.append([VLANSplit(), 1, 1])

    ## OUTPUT TESTS ##
    def output_test(vids, double_tag=False, default_gate=0):
        def gen_packet(vid):
            eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
            vlan1 = scapy.Dot1AD(vlan=vid)
            vlan2 = scapy.Dot1Q(vlan=vid)
            ip = scapy.IP()
            udp = scapy.UDP(sport=10001, dport=10002)
            payload = 'truculence'
            single_tag = eth / vlan2 / ip / udp / payload
            double_tag = eth / vlan1 / vlan2 / ip / udp / payload
            return single_tag, double_tag

        def gen_untagged_packet():
            eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
            ip = scapy.IP()
            udp = scapy.UDP(sport=10001, dport=10002)
            payload = 'truculence'
            pkt = eth / ip / udp / payload
            return pkt

        expected = []

        for vid in vids:
            p, pp = gen_packet(vid)
            q = gen_untagged_packet()
            if vid >= 0:
                if not double_tag:
                    expected.append({
                        'input_port': 0,
                        'output_port': vid,
                        'input_packet': p,
                        'output_packet': q})
                else:
                    expected.append({
                        'input_port': 0,
                        'output_port': vid,
                        'input_packet': pp,
                        'output_packet': p})
            else:
                expected.append({
                    'input_port': 0,
                    'output_port': default_gate,
                    'input_packet': q,
                    'output_packet': q})
        return [VLANSplit(), 1, 30, expected]

    OUTPUT_TEST_INPUTS.append(output_test([1, 17, -1, 29, 10, 13, 7]))
    OUTPUT_TEST_INPUTS.append(output_test([1, 17, -1, 29, 10, 13, 7], True))
