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

        s0.send(bytes(orig))
        natted_str = s1.recv(2048)
        natted = scapy.Ether(natted_str)
        # The NAT module can choose an arbitrary port/id.  We cannot test it,
        # we have to read from the output.
        l4_natted = l4_orig.copy()
        if type(l4_natted) == scapy.ICMP:
            l4_natted.id = natted.payload.payload.id
        else:
            l4_natted.sport = natted.payload.payload.sport
        assert bytes(eth / ip_natted / l4_natted / l7) == natted_str

        l4_reply = swap_l4(natted.payload.payload)
        reply = eth / ip_reply / l4_reply / l7

        s1.send(bytes(reply))
        unnatted_str = s0.recv(2048)
        assert bytes(eth / ip_unnatted / swap_l4(l4_orig) / l7) == unnatted_str

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
