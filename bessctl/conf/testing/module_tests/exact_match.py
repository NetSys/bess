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

# Crash test -- generate a bunch of rules and stuff packets through
em0 = ExactMatch(fields=[{'offset': 23, 'num_bytes': 1},  # random fields, I have no idea what these are
                         {'offset': 2, 'num_bytes': 2},
                         {'offset': 29, 'num_bytes': 1}])

em0.add(fields=[{'value_bin':b'\xff'}, {'value_bin':b'\x23\xba'}, {'value_bin':b'\x34'}], gate=0)
em0.add(fields=[{'value_bin':b'\xff'}, {'value_bin':b'\x34\xaa'}, {'value_bin':b'\x12'}], gate=1)
em0.add(fields=[{'value_bin':b'\xba'}, {'value_bin':b'\x33\xaa'}, {'value_bin':b'\x22'}], gate=2)
em0.add(fields=[{'value_bin':b'\xaa'}, {'value_bin':b'\x34\xba'}, {'value_bin':b'\x32'}], gate=3)
em0.add(fields=[{'value_bin':b'\x34'}, {'value_bin':b'\x34\x7a'}, {'value_bin':b'\x52'}], gate=4)
em0.add(fields=[{'value_bin':b'\x12'}, {'value_bin':b'\x34\x7a'}, {'value_bin':b'\x72'}], gate=5)

CRASH_TEST_INPUTS.append([em0, 1, 6])


# Output test over fields -- just make sure packets go out right ports
em1 = ExactMatch(fields=[{'offset': 26, 'num_bytes': 4},
                         {'offset': 30, 'num_bytes': 4}])  # ip src and dst

em1.add(fields=[{'value_bin':aton('65.43.21.00')}, {'value_bin':aton('12.34.56.78')}], gate=1)
em1.add(fields=[{'value_bin':aton('00.12.34.56')}, {'value_bin':aton('12.34.56.78')}], gate=2)
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
        fields=[{"attr_name": "sangjin", "num_bytes": 2}],masks=[{"value_bin": b'\xff\xf0'}])
    em2.add(fields=[{'value_bin':b'\x88\x80'}], gate=1)
    em2.add(fields=[{'value_bin':b'\x77\x70'}], gate=2)
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
