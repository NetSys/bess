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
rd0 = RandomDrop(drop_rate=0)
CRASH_TEST_INPUTS.append([rd0, 1, 1])

## OUTPUT TESTS ##
rd1 = RandomDrop(drop_rate=0)
test_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
OUTPUT_TEST_INPUTS.append([rd1, 1, 1,
                           [{'input_port': 0,
                               'input_packet': test_packet,
                                'output_port': 0,
                                'output_packet': test_packet}]])

## CUSTOM TESTS ##
def create_drop_test(rate):
    def equal_with_noise(a, b, threshold):
        return abs((a - b)) <= threshold
    def drop_test():
        src = Source()
        rd2 = RandomDrop(drop_rate=rate)
        rwtemp = [
            bytes(gen_packet(
                scapy.UDP,
                "172.12.0.3",
                "127.12.0.4")),
            bytes(gen_packet(
                scapy.TCP,
                "192.168.32.4",
                "1.2.3.4"))]
        a = Measure()
        b = Measure()
        src -> b -> Rewrite(templates=rwtemp) -> rd2 -> a -> Sink()

        bess.resume_all()
        time.sleep(2)
        bess.pause_all()

        # Measure the ratio of packets dropped
        ratio = float(a.get_summary().packets) / b.get_summary().packets
        assert equal_with_noise(ratio, 1 - rate, 0.05)
    return drop_test

CUSTOM_TEST_FUNCTIONS.extend([create_drop_test(0.5), create_drop_test(0.75), create_drop_test(0.9), create_drop_test(0.3)])
