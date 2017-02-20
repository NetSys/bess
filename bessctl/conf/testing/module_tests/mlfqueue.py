import sugar
from module import *
from port import *

#ensures that given infinite input that the module does not crash.
def crash_test():
    return [MLFQueue(num_levels=5, batch_size=32, init_load=10.0), 1, 1]

def basic_output_test():
    def gen_packet_list(protocol, input_ip, output_ip, num_pkts):
        packet_list = []
        for i in range(num_pkts):
            cur_pkt = gen_packet(protocol, input_ip, output_ip)
            packet_list.append({'input_port': 0, 'input_packet': cur_pkt,
                                    'output_port': 0, "output_packet": cur_pkt})
        return packet_list


    single_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
    MonitorTask(single_basic, 0)

    out = []
    single_packet= gen_packet_list(scapy.TCP, '22.22.22.22', '22.22.22.22', 1)
    out.append([single_basic,  # test this module
                               1, 1,  # it has one input port and one output port
                               single_packet])
    batch_basic = MLFQueue(num_levels=5, batch_size=5, init_load=10.0)
    packet_list = gen_packet_list(scapy.TCP, '22.22.22.1', '22.22.22.1', 2)
    packet_list += gen_packet_list(scapy.TCP, '22.22.11.1', '22.22.11.1', 2)
    packet_list += gen_packet_list(scapy.TCP, '22.11.11.1', '22.11.11.1', 1)
    out.append([batch_basic,  # test this module
                               1, 1,  # it has one input port and one output port
                               packet_list])
    return out

def fairness_output_test():
    bess.reset_all()

    test_packet1 = gen_packet(scapy.TCP, '22.11.11.1', '22.11.11.1')
    test_packet2 = gen_packet(scapy.TCP, '22.22.11.1', '22.22.11.1')
    # buf::Buffer()
    m1::Measure()
    src1::Source()-> Rewrite(templates=[test_packet1]) -> m1
    src2::Source() -> Rewrite(templates= [test_packet2]) -> m1
    m1 -> queue::MLFQueue(num_levels= 5, batch_size=20, init_load= 100) \
        -> m2::Measure() -> ACL(rules=[{'src_ip': '22.22.11.1', 'drop': False}]) \
        -> m3::Measure() -> Sink()

    bess.add_tc('rr', policy='round_robin', priority=0)
    bess.add_tc('fast', policy='rate_limit', resource='packet', limit={'packet': 9000000}, parent='rr')
    bess.add_tc('fast_leaf', policy='leaf', parent='fast')

    bess.add_tc('slow', policy='rate_limit', resource='packet', limit={'packet': 2000000}, parent='rr')
    bess.add_tc('slow_leaf', policy='leaf', parent='slow')

    bess.add_tc('medium', policy='rate_limit', resource='packet', limit={'packet': 5000000}, parent='rr')
    bess.add_tc('medium_leaf', policy='leaf', parent='medium')

    bess.attach_task(queue.name, tc= "medium_leaf")
    bess.attach_task(src1.name, tc='fast_leaf')
    bess.attach_task(src2.name, tc='slow_leaf')

    bess.resume_all()
    time.sleep(10000)
    # bess.pause_all()
    # print m1.get_summary()
    # print m2.get_summary()
    # print m3.get_summary()
    # share = float(m1.get_summary().packets)/m2.get_summary().packets
    # assert equal_with_noise(share, .4, .1)



# OUTPUT_TEST_INPUTS += basic_output_test()
CUSTOM_TEST_FUNCTIONS.append(fairness_output_test)
# CRASH_TEST_INPUTS.append(crash_test())
