# Copyright (c) 2017, The Regents of the University of California.
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


class BessModuleConstraintTest(BessModuleTestCase):

    def test_queue(self):
        # This is taken from queue.bess
        src = Source()
        src -> queue::Queue() \
            -> VLANPush(tci=2) \
            -> Sink()

        bess.add_tc('fast', policy='rate_limit',
                    resource='packet', limit={'packet': 9000000})
        src.attach_task('fast')

        bess.add_tc('slow', policy='rate_limit',
                    resource='packet', limit={'packet': 1000000})
        queue.attach_task('slow')

        self.assertFalse(bess.check_constraints())

    def test_nat(self):
        nat_config = [{'ext_addr': '192.168.1.1'}]
        # From nat.bess -- check that revisiting the same module works
        # correctly.
        nat = NAT(ext_addrs=nat_config)

        # Swap src/dst MAC
        mac = MACSwap()

        # Swap src/dst IP addresses / ports
        ip = IPSwap()

        Source() -> 0:nat:1 -> mac -> ip -> 1:nat:0 -> Sink()

        self.assertFalse(bess.check_constraints())

    def test_nat_queue(self):
        nat_config = [{'ext_addr': '192.168.1.1'}]
        # Check a combination.
        nat = NAT(ext_addrs=nat_config)

        # Swap src/dst IP addresses / ports
        ip = IPSwap()

        Source() -> 0:nat:1 -> Queue() -> ip -> 1:nat:0 -> Sink()

        self.assertFalse(bess.check_constraints())

    def test_nat_negative(self):
        nat_config = [{'ext_addr': '192.168.1.1'}]
        src0 = Source()
        src1 = Source()
        bess.add_worker(0, 0)
        bess.add_worker(1, 1)
        nat = NAT(ext_addrs=nat_config)
        src0 -> 0: nat: 1 -> Sink()
        src1 -> 1: nat: 0 -> Sink()
        src0.attach_task(wid=0)
        src1.attach_task(wid=1)

        with self.assertRaises(bess.ConstraintError):
            bess.check_constraints()


suite = unittest.TestLoader().loadTestsFromTestCase(BessModuleConstraintTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
