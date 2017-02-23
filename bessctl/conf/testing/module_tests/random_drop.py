import sugar
from module import *
from port import *

## CRASH TESTS ##
rd0 = RandomDrop(drop_rate=0)
CRASH_TEST_INPUTS.append([rd0, 1, 1])

## OUTPUT TESTS ##
rd1 = RandomDrop(drop_rate=0)
test_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
OUTPUT_TEST_INPUTS.append([rd1, 1, 1,
                           [{'input_port': 0,
                               'input_packet': test_packet,
                                'output_port': 0,
                                'output_packet': test_packet}]])

## CUSTOM TESTS ##
def create_drop_test(rate):
    def equal_with_noise(a, b, threshold):
        return abs((a - b)) <= threshold
    def drop_test():
        src = Source()
        rd2 = RandomDrop(drop_rate=rate)
        rwtemp = [
            gen_packet(
                scapy.UDP,
                "172.12.0.3",
                "127.12.0.4"),
            gen_packet(
                scapy.TCP,
                "192.168.32.4",
                "1.2.3.4")]
        a = Measure()
        b = Measure()

        src -> b -> Rewrite(templates=rwtemp) -> rd2 -> a -> Sink()

        bess.resume_all()
        time.sleep(2)
        bess.pause_all()

        # Measure the ratio of packets dropped
        ratio = float(a.get_summary().packets) / b.get_summary().packets
        assert equal_with_noise(ratio, 1 - rate, 0.05)
    return drop_test

CUSTOM_TEST_FUNCTIONS.extend([create_drop_test(0.5), create_drop_test(0.75), create_drop_test(0.9), create_drop_test(0.3)])
