import sugar
from module import *
from port import *

#ensures that given infinite input that the module does not crash.
def crash_test():
    return [MLFQueue(num_levels=5, batch_size=32, init_load=10.0), 1, 1]

def single_output_test():
    single_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
    MonitorTask(single_basic, 0)
    sockets, ports = set_up_module_ports(single_basic, 1, 1)
    single_packet= gen_batch(1, scapy.TCP, '22.22.22.22', '22.22.22.22')

    send_batches(sockets, [{'input_port': 0, 'input_batch': single_packet}])
    test_output_batches(single_basic, sockets, [{'output_port': 0, 'output_batch': [single_packet]}])


CUSTOM_TEST_FUNCTIONS += [basic_output_test]
CRASH_TEST_INPUTS.append(crash_test())
