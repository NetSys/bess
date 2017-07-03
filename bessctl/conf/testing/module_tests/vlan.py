import scapy.all as scapy

## CRASH TESTS ##
CRASH_TEST_INPUTS.append([VLANSplit(), 1, 1])

## OUTPUT TESTS ##
def output_test(vids, double_tag=False, default_gate=0):
    def gen_packet(vid):
        eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
        vlan1 = scapy.Dot1AD(vlan=vid)
        vlan2 = scapy.Dot1Q(vlan=vid)
        ip = scapy.IP()
        udp = scapy.UDP(sport=10001, dport=10002)
        payload = 'truculence'
        single_tag = eth / vlan2 / ip / udp / payload
        double_tag = eth / vlan1 / vlan2 / ip / udp / payload
        return single_tag, double_tag

    def gen_untagged_packet():
        eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
        ip = scapy.IP()
        udp = scapy.UDP(sport=10001, dport=10002)
        payload = 'truculence'
        pkt = eth / ip / udp / payload
        return pkt

    expected = []

    for vid in vids:
        p, pp = gen_packet(vid)
        q = gen_untagged_packet()
        if vid >= 0:
            if not double_tag:
                expected.append({
                    'input_port': 0,
                    'output_port': vid,
                    'input_packet': p,
                    'output_packet': q})
            else:
                expected.append({
                    'input_port': 0,
                    'output_port': vid,
                    'input_packet': pp,
                    'output_packet': p})
        else:
            expected.append({
                'input_port': 0,
                'output_port': default_gate,
                'input_packet': q,
                'output_packet': q})
    return [VLANSplit(), 1, 150, expected]

OUTPUT_TEST_INPUTS.append(output_test([1, 100, 77, -1, 149, 50, 100, -1]))
OUTPUT_TEST_INPUTS.append(output_test([100, 77, -1, 149, 50, 100, -1, 33, 70]))
OUTPUT_TEST_INPUTS.append(output_test([100, 77, -1, 149, 50, 100, -1, 33, 70], True))
