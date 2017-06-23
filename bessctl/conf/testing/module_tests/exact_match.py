# Crash test -- generate a bunch of rules and stuff packets through
em0 = ExactMatch(fields=[{'offset': 23, 'size': 1},  # random fields, I have no idea what these are
                         {'offset': 2, 'size': 2},
                         {'offset': 29, 'size': 1}],

em0.add(fields=[b'\xff', b'\x23\xba', b'\x34'], gate=0)
em0.add(fields=[b'\xff', b'\x34\xaa', b'\x12'], gate=1)
em0.add(fields=[b'\xba', b'\x33\xaa', b'\x22'], gate=2)
em0.add(fields=[b'\xaa', b'\x34\xba', b'\x32'], gate=3)
em0.add(fields=[b'\x34', b'\x34\x7a', b'\x52'], gate=4)
em0.add(fields=[b'\x12', b'\x34\x7a', b'\x72'], gate=5)

CRASH_TEST_INPUTS.append([em0, 1, 6])


# Output test over fields -- just make sure packets go out right ports
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

# Output test over fields -- just make sure packets go out right ports


def exactmatch_test_with_metadata():
    # One exact match field
    em2 = ExactMatch(
        fields=[{"attribute": "sangjin", "size": 2, "mask_bin": b'\xff\xf0'}])
    em2.add(fields=[b'\x88\x80'], gate=1)
    em2.add(fields=[b'\x77\x70'], gate=2)
    em2.set_default_gate(gate=0)

    # Only need one test packet
    eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
    ip = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
    udp = scapy.UDP(sport=10001, dport=10002)
    payload = 'helloworld'
    test_packet_in = ip / udp / payload

    # Three kinds of metadata tags
    metadata = []

    metadata.append(SetMetadata(
        attrs=[{"name": "sangjin", "size": 2, "value_bin": b'\x77\x90'}]))
    metadata.append(SetMetadata(
        attrs=[{"name": "sangjin", "size": 2, "value_bin": b'\x88\x80'}]))
    metadata.append(SetMetadata(
        attrs=[{"name": "sangjin", "size": 2, "value_bin": b'\x77\x70'}]))

    # And a merge module
    merger = Merge()
    merger -> em2

    sockets = []
    indrivers = []
    outdrivers = []
    for i in range(3):  # three input ports for three metadata tags
        sockname = "exactmatch_metadata" + SCRIPT_STARTTIME + str(i)
        socket_port, mysocket = gen_socket_and_port(sockname)

        sockets.append(mysocket)
        indrivers.append(PortInc(port=sockname))
        outdrivers.append(PortOut(port=sockname))

        indrivers[i] -> metadata[i] -> merger
        em2: i -> outdrivers[i]

    bess.resume_all()

    # Now run the packets through
    for i in range(3):
        sockets[i].send(bytes(test_packet_in))
        return_data = sockets[i].recv(2048)
        assert(bytes(return_data) == bytes(test_packet_in))

    bess.pause_all()


CUSTOM_TEST_FUNCTIONS.append(exactmatch_test_with_metadata)
