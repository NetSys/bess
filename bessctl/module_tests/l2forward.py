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

from test_utils import *


class BessL2ForwardTest(BessModuleTestCase):

    def test_l2forward(self):
        l2fib = L2Forward()

        l2fib.add(entries=[{'addr': '00:01:02:03:04:05', 'gate': 64},
                           {'addr': 'aa:bb:cc:dd:ee:ff', 'gate': 1},
                           {'addr': '11:11:11:11:11:22', 'gate': 2}])
        with self.assertRaises(bess.Error):
            l2fib.add(entries=[{'addr': '00:01:02:03:04:05', 'gate': 0}])

        ret = l2fib.lookup(addrs=['aa:bb:cc:dd:ee:ff', '00:01:02:03:04:05'])
        self.assertEquals(ret.gates, [1, 64])

        l2fib.delete(addrs=['00:01:02:03:04:05'])
        with self.assertRaises(bess.Error):
            l2fib.delete(addrs=['00:01:02:03:04:05'])

suite = unittest.TestLoader().loadTestsFromTestCase(BessL2ForwardTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
