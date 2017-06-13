# Crash test -- generate a bunch of rules and stuff packets through
em0 = ExactMatch(fields=[{'offset': 23, 'size': 1},  # random fields, I have no idea what these are
                         {'offset': 2, 'size': 2},
                         {'offset': 29, 'size': 1}])

em0.add(fields=[chr(255), chr(0) + chr(12), chr(55)], gate=0)
em0.add(fields=[chr(255), chr(12) + chr(34), chr(55)], gate=1)
em0.add(fields=[chr(255), chr(12) + chr(34), chr(255)], gate=2)
em0.add(fields=[chr(19), chr(12) + chr(34), chr(255)], gate=3)
em0.add(fields=[chr(19), chr(66) + chr(0), chr(2)], gate=4)
em0.add(fields=[chr(123), chr(4) + chr(0), chr(2)], gate=5)

CRASH_TEST_INPUTS.append([em0, 1, 6])


#Output test -- just make sure packets go out right ports
em1 = ExactMatch(fields=[{'offset': 26, 'size': 4},
                         {'offset': 30, 'size': 4}])  # ip src and dst

em1.add(fields=[aton('65.43.21.00'), aton('12.34.56.78')], gate=1)
em1.add(fields=[aton('00.12.34.56'), aton('12.34.56.78')], gate=2)
em1.set_default_gate(gate=3)

test_packet1 = gen_packet(scapy.TCP, '65.43.21.00', '12.34.56.78')  # match 1
test_packet2 = gen_packet(scapy.TCP, '00.12.34.56', '12.34.56.78')  # match 2
test_packet3 = gen_packet(scapy.TCP, '00.12.33.56',
                          '12.34.56.78')  # match nothing

OUTPUT_TEST_INPUTS.append([em1, 1, 4,
                           [{'input_port': 0,
                             'input_packet': test_packet1,
                             'output_port': 1,
                             'output_packet': test_packet1},
                            {'input_port': 0,
                             'input_packet': test_packet2,
                             'output_port': 2,
                             'output_packet': test_packet2},
                               {'input_port': 0,
                                'input_packet': test_packet3,
                                'output_port': 3,
                                'output_packet': test_packet3},
                               {'input_port': 0,
                                'input_packet': None,
                                'output_port': 0,
                                'output_packet': None}]])
