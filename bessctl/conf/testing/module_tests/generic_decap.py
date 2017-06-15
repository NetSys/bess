#CRASH TEST
gd0 = GenericDecap(bytes=0)
CRASH_TEST_INPUTS.append([gd0, 1, 1])

gd1 = GenericDecap(bytes=23)
CRASH_TEST_INPUTS.append([gd1, 1, 1])

#OUTPUT TESTS

#test strip off ether
gd2 = GenericDecap(bytes=14)
eth = scapy.Ether(src='de:ad:be:ef:12:34', dst='12:34:de:ad:be:ef')
ip = scapy.IP(src="1.2.3.4", dst="2.3.4.5", ttl=98)
udp = scapy.UDP(sport=10001, dport=10002)
payload = 'helloworldhelloworldhelloworld'

eth_packet_in = eth/ip/udp/payload
eth_packet_out = ip/udp/payload

OUTPUT_TEST_INPUTS.append([gd2, 1, 1, 
	[{'input_port': 0, 'input_packet': eth_packet_in,
	'output_port': 0, 'output_packet': eth_packet_out}]])
