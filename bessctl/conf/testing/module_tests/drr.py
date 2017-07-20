# Copyright (c) 2017, Joshua Stone.
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

# ensures that given infinite input that the module does not crash.


def crash_test():
    return [DRR(), 1, 1]

# tests to make that inividual packets gets through the module


def basic_output_test():

    # produces the number of duplicate packets specified by num_pkts and uses the provided
    # the specifications in the packet along with dummy ports.
    def gen_packet_list(protocol, input_ip, output_ip, num_pkts):
        packet_list = []
        for i in range(num_pkts):
            cur_pkt = gen_packet(protocol, input_ip, output_ip)
            packet_list.append({'input_port': 0, 'input_packet': cur_pkt,
                                'output_port': 0, "output_packet": cur_pkt})
        return packet_list

    single_basic = DRR(num_flows=2, max_flow_queue_size=100)
    monitor_task(single_basic, 0)

    out = []
    single_packet = gen_packet_list(scapy.TCP, '22.22.22.22', '22.22.22.22', 1)
    out.append([single_basic,  # test this module
                1, 1,  # it has one input port and one output port
                single_packet])
    batch_basic = DRR(num_flows=4, max_flow_queue_size=100)
    monitor_task(batch_basic, 0)
    packet_list = gen_packet_list(scapy.TCP, '22.22.22.1', '22.22.22.1', 2)
    packet_list += gen_packet_list(scapy.TCP, '22.22.11.1', '22.22.11.1', 2)
    packet_list += gen_packet_list(scapy.TCP, '22.11.11.1', '22.11.11.1', 1)
    out.append([batch_basic,  # test this module
                1, 1,  # it has one input port and one output port
                packet_list])
    return out


# tests the fairness of 2 and 5 flows setup using the inner helper function.
def fairness_test():
    # Takes the number of flows n, the quantum to give drr, the list packet rates for each flow
    # and the packet rate for the module. runs this setup for five seconds and tests that
    # throughput for each flow had a jaine fairness of atleast .95.
    def fairness_n_flow_test(n, quantum, rates, module_rate):
        err = bess.reset_all()

        packets = []
        exm = ExactMatch(fields=[{'offset': 26, 'num_bytes': 4}])
        for i in range(1, n + 1):
            packets.append(
                bytes(gen_packet(scapy.TCP, '22.11.11.' + str(i), '22.22.11.' + str(i))))
            exm.add(
                fields=[{'value_bin': socket.inet_aton('22.11.11.' + str(i))}], gate=i)

        me_in = Measure()
        src = []
        measure_in = []
        rewrites = []
        for i in range(0, n):
            src.append(Source())
            measure_in.append(Measure())
            rewrites.append(Rewrite(templates=[packets[i]]))
            src[i] -> rewrites[i] -> measure_in[i] -> me_in

        me_out = Measure()
        snk = Sink()
        q = DRR(num_flows=n + 1, quantum=quantum)
        me_in -> q -> me_out -> exm

        measure_out = []
        for i in range(1, n + 1):
            measure_out.append(Measure())
            exm:i -> measure_out[i - 1] -> snk

        for i in range(0, n):
            bess.add_tc('r' + str(i), policy='rate_limit', resource='packet',
                        limit={'packet': rates[i]})
            src[i].attach_task(parent='r' + str(i))

        bess.add_tc('output', policy='rate_limit', resource='packet',
                    limit={'packet': module_rate})
        q.attach_task(parent='output')

        bess.resume_all()
        time.sleep(5)
        bess.pause_all()
        f = lambda m: (float)((m.get_summary().packets)**2)
        square_sum = 0
        for i in range(n):
            square_sum += f(measure_out[i])
        square_sum *= n

        if square_sum == 0:
            fair = 0
        else:
            fair = f(me_out) / square_sum
        assert abs(.99 - fair) <= .05

    fairness_n_flow_test(2, 1000, [80000, 20000], 30000)
    fairness_n_flow_test(
        5, 1000, [110000, 200000, 70000, 60000, 40000], 150000)

    ten_flows = [210000, 120000, 130000, 160000,
                 100000, 105000, 90000, 70000, 60000, 40000]
    fairness_n_flow_test(10, 1000, ten_flows, 300000)

    # hund_flows= []
    # cur = 200000
    # for i in range(100):
        # hund_flows.append(cur)
        # cur -= 1600
    # fairness_n_flow_test(100, 1000, hund_flows, 300000)

OUTPUT_TEST_INPUTS += basic_output_test()
CUSTOM_TEST_FUNCTIONS.append(fairness_test)
CRASH_TEST_INPUTS.append(crash_test())
