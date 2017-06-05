## CRASH TESTS ##
buf0 = Buffer() #Buffer takes no parameters
CRASH_TEST_INPUTS.append([buf0, 1, 1])

## INPUT TESTS ##
batch_size = 32

buf1 = Buffer()
test_packet = gen_packet(scapy.TCP, '22.22.22.22', '22.22.22.22')
test_packet2 = gen_packet(scapy.TCP, '33.22.22.22', '22.22.22.22')
test_data = []

#Should withhold data until it has a full batch to push through.
for i in range(batch_size - 1):
    test_data.append({'input_port' : 0, 'input_packet': test_packet,
        'output_port' : 0, 'output_packet': None})

test_data.append({'input_port' : 0, 'input_packet': test_packet2,
        'output_port' : 0, 'output_packet': test_packet})

OUTPUT_TEST_INPUTS.append([buf1, 1, 1, test_data])
