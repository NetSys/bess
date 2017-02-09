import sugar
from module import *
from port import *

#basic output test to make sure that singe packet batches
# inputted come out unalterred.
def basic_output_test():
    #generates input packets for an output test
    #where you expect the same packets out
    def gen_packet_list(protocol, input_ip, output_ip, num_pkts):
        packet_list = []
        for i in range(num_pkts):
            cur_pkt = gen_packet(protocol, input_ip, output_ip)
            packet_list.append({'input_port': 0, 'input_packet': cur_pkt,
                                    'output_port': 0, "output_packet": cur_pkt})
        return packet_list

    out = []
    single_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
    single_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
    out.append([single_basic,  # test this module
                               1, 1,  # it has one input port and one output port
                               [{'input_port': 0,
                                   'input_packet': single_packet,  # send test_packet in on input port 0
                                    'output_port': 0,
                                    'output_packet': single_packet}]])
    batch_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
    packet_list = []
    packet_list = gen_packet_list(scapy.TCP, '22.22.22.22', '22.22.22.22', 2)
    packet_list += gen_packet_list(scapy.TCP, '31.14.15.69', '31.14.15.69', 3)
    out.append([batch_basic,  # test this module
                               1, 1,  # it has one input port and one output port
                               packet_list])
    return out

#ensures that given infinite input that the module does not crash.
def crash_test():
    return [MLFQueue(num_levels=5, batch_size=32, init_load=10.0), 1, 1]

OUTPUT_TEST_INPUTS += basic_output_test()
CRASH_TEST_INPUTS.append(crash_test())
