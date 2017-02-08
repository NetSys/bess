import sugar
import scapy.all as scapy
from module import *
from port import *


## CRASH TESTS ##
m0 = UpdateTTL()
CRASH_TEST_INPUTS.append([m0, 1, 1])

## OUTPUT TESTS ##
# Decrement Test
m1 = UpdateTTL()
in_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22', ip_ttl=2)
out_packet = scapy.Ether(in_packet)
out_packet.ttl -= 1
out_packet = str(out_packet)
OUTPUT_TEST_INPUTS.append([m1, 1, 1,
                           [{'input_port': 0,
                             'input_packet': in_packet,
                             'output_port': 0,
                             'output_packet': out_packet}]])

# Drop test
m2 = UpdateTTL()
drop_packet0 = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22', ip_ttl=0)

drop_packet1 = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22', ip_ttl=1)
OUTPUT_TEST_INPUTS.append([m2, 1, 1,
                           [{'input_port': 0,
                             'input_packet': drop_packet0,
                             'output_port': 0,
                             'output_packet': None},
                            {'input_port': 0,
                             'input_packet': in_packet,
                             'output_port': 0,
                             'output_packet': out_packet},
                            {'input_port': 0,
                             'input_packet': drop_packet1,
                             'output_port': 0,
                             'output_packet': None}]])

# TODO: If Checksum code is modified and integrated make sure to write tests for it

