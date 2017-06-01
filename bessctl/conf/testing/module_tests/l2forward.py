import scapy.all as scapy


def test_lookup():
    l2fib = L2Forward()

    # Adding entry
    ret = l2fib.add(entries=[{'addr': '00:01:02:03:04:05', 'gate': 64},
                             {'addr': 'aa:bb:cc:dd:ee:ff', 'gate': 1},
                             {'addr': '11:11:11:11:11:22', 'gate': 2}])

    # Adding entry again expecting failure
    try:
        l2fib.add(entries=[{'addr': '00:01:02:03:04:05', 'gate': 0}])
    except Exception as e:
        pass
    else:
        assert False, 'Failure was expected'

    # Querying entry
    ret = l2fib.lookup(addrs=['aa:bb:cc:dd:ee:ff', '00:01:02:03:04:05'])
    assert ret.gates == [1, 64], 'Incorrect response'

    # Removing Entry
    ret = l2fib.delete(addrs=['00:01:02:03:04:05'])

    # Querying entry again expecting failure'
    try:
        l2fib.delete(addrs=['00:01:02:03:04:05'])
    except Exception as e:
        pass
    else:
        assert False, 'failure was expected'


CUSTOM_TEST_FUNCTIONS.append(test_lookup)
