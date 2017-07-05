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

# CRASH TEST ##

filters = [
        "tcp src port 92",
        "len <= 1000",
        "ether proto 0x800",
        "ip proto 47 or ip6 proto 47",
        "ip host 22.22.22.22"
        ]

filter0 = {"priority": 0, "filter": filters[0], "gate": 1}
bpf0::BPF()
bpf0.add(filters=[filter0])
CRASH_TEST_INPUTS.append([bpf0, 1, 2])

bpf1::BPF()
for i, exp in enumerate(filters):
    bpf1.add(filters=[{"priority": i, "filter": exp, "gate": i}])

CRASH_TEST_INPUTS.append([bpf1, 1, len(filters)])

# OUTPUT TEST ##

# Test basic output/steering with single rule
bpf2::BPF()
bpf2.add(filters=[filter0])
packet1 = gen_packet(scapy.UDP, '12.34.56.78', '12.34.56.78')
packet2 = gen_packet(scapy.TCP, '12.34.56.78', '12.34.56.78', srcport=92)

OUTPUT_TEST_INPUTS.append([bpf2, 1, 2,
    [{'input_port': 0,
        'input_packet': packet1,
        'output_port': 0,
        'output_packet': packet1},
     {'input_port': 0,
         'input_packet': packet2,
         'output_port': 1,
         'output_packet': packet2}]])

# Test multiple rules with priorities
bpf3::BPF()
bpf3.add(filters=[{"priority": 2, "filter": filters[0], "gate": 1}])
bpf3.add(filters=[{"priority": 1, "filter": filters[4], "gate": 2}])
packet1 = gen_packet(scapy.UDP, '22.22.22.22', '12.34.56.78', srcport=700)
packet2 = gen_packet(scapy.TCP, '12.34.56.78', '22.22.22.22', srcport=92)
packet3 = gen_packet(scapy.TCP, '12.34.56.78', '12.34.56.78', srcport=700)

OUTPUT_TEST_INPUTS.append([bpf3, 1, 3,
    [{'input_port': 0,
        'input_packet': packet1,
        'output_port': 2,
        'output_packet': packet1},
     {'input_port': 0,
         'input_packet': packet2,
         'output_port': 1,
         'output_packet': packet2},
     {'input_port': 0,
         'input_packet': packet3,
         'output_port': 0,
         'output_packet': packet3}]])
