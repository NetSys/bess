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

import scapy.all as scapy


## CRASH TESTS ##
m0 = UpdateTTL()
CRASH_TEST_INPUTS.append([m0, 1, 1])

## OUTPUT TESTS ##
# Decrement Test
m1 = UpdateTTL()
in_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22', ip_ttl=2)
out_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22', ip_ttl=1)

OUTPUT_TEST_INPUTS.append([m1, 1, 1,
                           [{'input_port': 0,
                             'input_packet': in_packet,
                             'output_port': 0,
                             'output_packet': out_packet}]])

# Drop test
m2 = UpdateTTL()
drop_packet0 = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22', ip_ttl=0)
drop_packet1 = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22', ip_ttl=1)

OUTPUT_TEST_INPUTS.append([m2, 1, 1,
                           [{'input_port': 0,
                             'input_packet': drop_packet0,
                             'output_port': 0,
                             'output_packet': None},
                            {'input_port': 0,
                             'input_packet': in_packet,
                             'output_port': 0,
                             'output_packet': out_packet},
                            {'input_port': 0,
                             'input_packet': drop_packet1,
                             'output_port': 0,
                             'output_packet': None}]])
