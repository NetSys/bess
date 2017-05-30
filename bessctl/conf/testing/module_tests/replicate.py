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


