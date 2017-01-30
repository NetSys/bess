import sugar
from module import *
from port import *

## CRASH TESTS ##
# Create a module and specify how many input and output ports it has.
# Test will generate a bunch of UDP packets and feed them in on all of the input ports,
# and drop any released packets into an output port.
# All this is testing is that the module (as you configure it) doesn't crash
# when it is fed traffic.
# Make sure every test input has a new module -- no reusing modules
# between tests.
fw0 = ACL(rules=[{'src_ip': '172.12.0.0/16', 'drop': False}])
# [test module fw0; it has one input port; it has one output port]#
CRASH_TEST_INPUTS.append([fw0, 1, 1])

# A more complicated configuration
fw1 = ACL(rules=[{'src_ip': '172.12.0.0/16',
                  'drop': False},
                 {'dst_ip': '192.168.32.4/32',
                  'dst_port': 4455,
                  'src_ip': '134.54.33.2/32',
                  'drop': False},
                 {'src_ip': '133.133.133.0/24',
                  'src_port': 43,
                  'dst_ip': '96.96.96.155/32',
                  'dst_port': 9,
                  'drop': False}])
CRASH_TEST_INPUTS.append([fw1, 1, 1])

# An edge case
fw2 = ACL(rules=[{'src_ip': '0.0.0.0/0', 'drop': False}])
CRASH_TEST_INPUTS.append([fw2, 1, 1])

## OUTPUT TESTS ##
# Create a module and then specify a list of packets that you want fed in to the module/
# expect to come out of the module.
# For each test, it will input one packet, and look for one packet released.
# If you don't want to input a packet (but still want to check an output), set the input
# to None. If you want to check that a packet is *not* output on a port, set the output
# to None.
fw3 = ACL(rules=[{'src_ip': '0.0.0.0/0', 'drop': False}])  # module to test
test_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
OUTPUT_TEST_INPUTS.append([fw3,  # test this module
                           1, 1,  # it has one input port and one output port
                           [{'input_port': 0,
                               'input_packet': test_packet,  # send test_packet in on input port 0
                                'output_port': 0,
                                'output_packet': test_packet}]])  # I expect test_packet to come out on output port 0

# You can also run two tests back to back.
fw4 = ACL(rules=[{'src_ip': '96.0.0.0/8', 'drop': False}])
test_packet2 = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
test_packet3 = gen_packet(scapy.TCP, '96.22.22.22', '22.22.22.22')
OUTPUT_TEST_INPUTS.append([fw4, 1, 1,  # test this module, it has one input port and one output port
                           [{'input_port': 0,
                               'input_packet': test_packet2,
                               'output_port': 0,
                               'output_packet': None},  # here I expect a packet to be dropped
                            {'input_port': 0,
                                'input_packet': test_packet3,
                                'output_port': 0,
                                'output_packet': test_packet3}]])  # And here I expect it to come through

## CUSTOM TESTS ##
# Some tests you might want to add could be more complicated than just checking inputs and ouputs.
# Here you can just define your own functions and link pipelines together. .
# If your test fails, raise an exception and the master script will catch and print it.
# This test is kind of stupid -> Its the same as the crash test, only with
# some custom traffic I specified.
def my_bonus_acl_test():
    fw5 = ACL(rules=[{'src_ip': '172.12.0.0/16',
                      'drop': False},
                     {'dst_ip': '192.168.32.4/32',
                      'dst_port': 4455,
                      'src_ip': '134.54.33.2/32',
                      'drop': False},
                     {'src_ip': '133.133.133.0/24',
                      'src_port': 43,
                      'dst_ip': '96.96.96.155/32',
                      'dst_port': 9,
                      'drop': False}])
    src = Source()
    rwtemp = [
        gen_packet(
            scapy.UDP,
            "172.12.0.3",
            "127.12.0.4"),
        gen_packet(
            scapy.TCP,
            "192.168.32.4",
            "1.2.3.4")]
    src -> Rewrite(templates=rwtemp) -> fw5 -> Sink()
    bess.resume_all()
    time.sleep(15)
    bess.pause_all()

CUSTOM_TEST_FUNCTIONS.append(my_bonus_acl_test)
