import scapy.all as scapy


## CRASH TESTS ##
m0 = ArpResponder()
CRASH_TEST_INPUTS.append([m0, 1, 1])

## OUTPUT TESTS ##
m1 = ArpResponder()
eth_header = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='ff:ff:ff:ff:ff:ff')
arp_header = scapy.ARP(op=1, pdst='1.2.3.4')
arp_req = eth_header/arp_header

m1.add(ip='1.2.3.4', mac_addr='A0:22:33:44:55:66')

arp_reply = arp_req.copy()
arp_reply[scapy.Ether].src = 'A0:22:33:44:55:66'
arp_reply[scapy.Ether].dst = '02:1e:67:9f:4d:ae'
arp_reply[scapy.ARP].op = 2

arp_reply[scapy.ARP].hwdst = arp_req[scapy.ARP].hwsrc
arp_reply[scapy.ARP].hwsrc = 'A0:22:33:44:55:66'

arp_reply[scapy.ARP].pdst = arp_req[scapy.ARP].psrc
arp_reply[scapy.ARP].psrc = '1.2.3.4'


OUTPUT_TEST_INPUTS.append([m1, 1, 1,
                           [{'input_port': 0,
                             'input_packet': arp_req,
                             'output_port': 0,
                             'output_packet': arp_reply}]])