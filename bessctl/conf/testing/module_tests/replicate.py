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

## CRASH TESTS ##
rep4 = Replicate(gates=[0,1,2,3])
CRASH_TEST_INPUTS.append([rep4, 1, 4])

rep10 = Replicate(gates=[0,1,2,3,4,5,6,7,8,9])
CRASH_TEST_INPUTS.append([rep10, 1, 10])

rep1 = Replicate(gates=[0])
CRASH_TEST_INPUTS.append([rep1, 1, 1])

## OUTPUT TESTS ##
rep3 = Replicate(gates=[0,1,2])
test_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
OUTPUT_TEST_INPUTS.append([rep3,  # test this module
                           1, 3,
                           [{'input_port': 0,
                               'input_packet': test_packet,
                                'output_port': 0,
                                'output_packet': test_packet},
                             {'input_port': 0,
                               'input_packet': None,
                                'output_port': 1,
                                'output_packet': test_packet},
                            {'input_port': 0,
                               'input_packet': None,
                                'output_port': 2,
                                'output_packet': test_packet}
                                ]])  # I expect test_packet to come out on all ports


