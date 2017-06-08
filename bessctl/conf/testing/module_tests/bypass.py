# Crash Test #
bp0 = Bypass()
CRASH_TEST_INPUTS.append([bp0, 1, 1])

# Output Test #
bp1 = Bypass()
test_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
OUTPUT_TEST_INPUTS.append([bp1,
                           1, 1,
                           [{'input_port': 0,
                               'input_packet': test_packet,
                                'output_port': 0,
                                'output_packet': test_packet},
                            {'input_port': 0,
                               'input_packet': None,
                                'output_port': 0,
                                'output_packet': None}]])
