import sugar
import scapy.all as scapy
import socket

from module import *
from port import *


# Test the packet mangling features with a single rule
def my_nat_simple_rule_test():
    def swap_l4(l4):
        ret = l4.copy()
        ret.remove_payload()
        if type(l4) == scapy.ICMP:
            ret.id = l4.id
        else:
            ret.sport = l4.dport
            ret.dport = l4.sport
        return ret

    def test_l4(l4_orig, s0, s1, ruleaddr):
        # There are 4 packets in this test:
        # 1. orig, 2. natted, 3. reply, 4. unnatted
        #
        # We're acting as 's0' and 's1' to test 'NAT'
        #
        # +----+    orig    +-------+   natted    +----+
        # |    |----------->|       |------------>|    |
        # | s0 |            |  NAT  |             | s1 |
        # |    |<-----------|       |<------------|    |
        # +----+  unnatted  +-------+    reply    +----+

        eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
        ip_orig = scapy.IP(src='172.16.0.2', dst='8.8.8.8')
        ip_natted = scapy.IP(src=ruleaddr, dst='8.8.8.8')
        ip_reply = scapy.IP(src='8.8.8.8', dst=ruleaddr)
        ip_unnatted = scapy.IP(src='8.8.8.8', dst='172.16.0.2')
        l7 = 'helloworld'

        orig = eth / ip_orig / l4_orig / l7

        s0.send(str(orig))
        natted_str = s1.recv(2048)
        natted = scapy.Ether(natted_str)
        # The NAT module can choose an arbitrary port/id.  We cannot test it,
        # we have to read from the output.
        l4_natted = l4_orig.copy()
        if type(l4_natted) == scapy.ICMP:
            l4_natted.id = natted.payload.payload.id
        else:
            l4_natted.sport = natted.payload.payload.sport
        assert str(eth / ip_natted / l4_natted / l7) == natted_str

        l4_reply = swap_l4(natted.payload.payload)
        reply = eth / ip_reply / l4_reply / l7

        s1.send(str(reply))
        unnatted_str = s0.recv(2048)
        assert str(eth / ip_unnatted / swap_l4(l4_orig) / l7) == unnatted_str

    nat0::NAT(ext_addrs=['192.168.1.1'])

    port0, s0 = gen_socket_and_port("NATcustom0_" + SCRIPT_STARTTIME)
    port1, s1 = gen_socket_and_port("NATcustom1_" + SCRIPT_STARTTIME)

    PortInc(port=port0.name) -> 0:nat0:0 -> PortOut(port=port1.name)
    PortInc(port=port1.name) -> 1:nat0:1 -> PortOut(port=port0.name)

    bess.resume_all()

    test_l4(scapy.UDP(sport=56797, dport=53), s0, s1, '192.168.1.1')
    test_l4(scapy.UDP(sport=56797, dport=53, chksum=0), s0, s1, '192.168.1.1')
    test_l4(scapy.TCP(sport=52428, dport=80), s0, s1, '192.168.1.1')
    test_l4(scapy.ICMP(), s0, s1, '192.168.1.1')

    bess.pause_all()


CUSTOM_TEST_FUNCTIONS.append(my_nat_simple_rule_test)
