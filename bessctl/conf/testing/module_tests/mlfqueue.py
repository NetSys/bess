import sugar
from module import *
from port import *

#basic output test to make sure that singe packet batches
# inputted come out unalterred.
# def basic_output_test():
#
#     out = []
#     single_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
#     single_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
#     out.append([single_basic,  # test this module
#                                1, 1,  # it has one input port and one output port
#                                [{'input_port': 0,
#                                    'input_packet': single_packet,  # send test_packet in on input port 0
#                                     'output_port': 0,
#                                     'output_packet': single_packet}]])
#     batch_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
#     packet_list = []
#     packet_list = gen_packet_list(scapy.TCP, '22.22.22.22', '22.22.22.22', 2)
#     packet_list += gen_packet_list(scapy.TCP, '31.14.15.69', '31.14.15.69', 3)
#     out.append([batch_basic,  # test this module
#                                1, 1,  # it has one input port and one output port
#                                packet_list])
#     return out

#ensures that given infinite input that the module does not crash.
def crash_test():
    return [MLFQueue(num_levels=5, batch_size=32, init_load=10.0), 1, 1]

def basic_output_test():
    single_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
    MonitorTask(single_basic, 0)
    sockets, ports = set_up_module_ports(single_basic, 1, 1)
    single_packet= gen_batch(1, scapy.TCP, '22.22.22.22', '22.22.22.22')

    send_batches(sockets, [{'input_port': 0, 'input_batch': single_packet}])
    test_output_batches(sockets, [{'output_port': 0, 'output_batch': [single_packet]}])


OUTPUT_TEST_INPUTS += basic_output_test()
CRASH_TEST_INPUTS.append(crash_test())
