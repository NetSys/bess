## Can't run Crash or Output tests because EtherEncap requires metadata to be set first #

def etherencap_crashtest():
    encap1 = EtherEncap()
    src = Source()
    metadata = SetMetadata(attrs=[{"name": "ether_src", "size": 6, "value_bin": b'\xde\xad\xbe\xef\x12\x34'},
        {"name": "ether_dst", "size": 6, "value_bin": b'\x12\x34\xde\xad\xbe\xef'},
        {"name" : "ether_type", "size": 2, "value_bin": b'\x08\x00'}])
    rwtemp = [
        bytes(gen_packet(
            scapy.UDP,
            "172.12.0.3",
            "127.12.0.4")),
        bytes(gen_packet(
            scapy.TCP,
            "192.168.32.4",
            "1.2.3.4"))]
    src -> Rewrite(templates=rwtemp) -> metadata -> encap1 -> Sink()
    bess.resume_all()
    time.sleep(5)
    bess.pause_all()


CUSTOM_TEST_FUNCTIONS.append(etherencap_crashtest)

## See if it correctly wraps IP packet in Ether header
def metadata_outputtest_fixedvalues():
    eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
    ip = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
    udp = scapy.UDP(sport=10001, dport=10002)
    payload = 'helloworld'

    test_packet_in = ip/udp/payload
    test_packet_out = eth/ip/udp/payload

    sockname = "metadata_fixedvalues" + SCRIPT_STARTTIME
    socket_port, mysocket = gen_socket_and_port(sockname)

    indriver = PortInc(port=sockname)
    outdriver = PortOut(port=sockname)

    encap2 = EtherEncap()
    metadata = SetMetadata(attrs=[{"name": "ether_src", "size": 6, "value_bin": b'\xde\xad\xbe\xef\x12\x34'},
        {"name": "ether_dst", "size": 6, "value_bin": b'\x12\x34\xde\xad\xbe\xef'},
        {"name" : "ether_type", "size": 2, "value_bin": b'\x08\x00'}])
    indriver -> metadata -> encap2 -> outdriver

    bess.resume_all()
    mysocket.send(bytes(test_packet_in))
    return_data = mysocket.recv(2048)
    bess.pause_all()
    assert(bytes(return_data) == bytes(test_packet_out))

CUSTOM_TEST_FUNCTIONS.append(metadata_outputtest_fixedvalues)
