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

# OUTPUT TESTS

module = IPChecksum()

eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
ip_wrong = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98, chksum=0x0000)
ip_right = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
udp = scapy.UDP(sport=10001, dport=10002)

payload = 'helloworldhelloworldhelloworld'

in_out = []

eth_in = eth / ip_wrong / udp / payload
eth_out = eth / ip_right / udp / payload
assert bytes(eth_in) != bytes(eth_out)
in_out.append({'input_packet': eth_in, 'output_packet': eth_out})

vlan = scapy.Dot1Q(vlan=6)

vlan_in = eth / vlan / ip_wrong / udp / payload
vlan_out = eth / vlan / ip_right / udp / payload
assert bytes(vlan_in) != bytes(vlan_out)
in_out.append({'input_packet': vlan_in, 'output_packet': vlan_out})

# scapy-python3 doesn't have Dot1AD
if hasattr(scapy, 'Dot1AD'):
    qinq = scapy.Dot1AD(vlan=5)

    qinq_in = eth / qinq / vlan / ip_wrong / udp / payload
    qinq_out = eth / qinq / vlan / ip_right / udp / payload
    assert bytes(qinq_in) != bytes(qinq_out)
    in_out.append({'input_packet': qinq_in, 'output_packet': qinq_out})

OUTPUT_TEST_INPUTS.append(
    [module, 1, 1, [dict(input_port=0, output_port=0, **x) for x in in_out]])
