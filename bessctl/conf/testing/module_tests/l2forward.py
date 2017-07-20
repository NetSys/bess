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
