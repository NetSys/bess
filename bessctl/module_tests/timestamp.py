# Copyright (c) 2018, Nefeli Networks, Inc.
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

from __future__ import print_function

from test_utils import *


# test both measure and timestamp (can't test them separately)
class BessTimestampTest(BessModuleTestCase):

    def test_timestamped_and_measured(self):
        udp = get_udp_packet(sip='172.12.0.3', dip='127.12.0.4')
        mmod = Measure(latency_ns_resolution=1, latency_ns_max=100000)
        Source() -> Rewrite(templates=[bytes(udp)]) -> Timestamp() -> \
            Bypass(cycles_per_batch=100) -> mmod -> Sink()
        self.bess.resume_all()
        # Run for .5 seconds to warm up ...
        time.sleep(0.5)
        # ... then clear accumulated stats.
        self.bess.run_module_command(mmod.name, 'clear', 'EmptyArg', {})
        # Run for 2 more seconds to accumulate real stats.
        time.sleep(2)
        self.bess.pause_all()
        self.assertBessAlive()
        pct = [0, 25, 50, 99, 100]
        stats = self.bess.run_module_command(mmod.name, 'get_summary',
                                             'MeasureCommandGetSummaryArg',
                                             {'latency_percentiles': pct})
        # There is no way to know if what the verbosity= argument below
        # is from here.  Note that stats contains, e.g. (on my laptop VM,
        # using the default resolution and max values):
        #
        # timestamp: 1517529594.72
        # packets: 64807616
        # bits: 43550717952
        # latency = {
        #   count: 64807616
        #   min_ns: 400
        #   avg_ns: 513
        #   max_ns: 295600
        #   total_ns: 33273840000
        #   percentile_values_ns: [400, 500, 500, 500, 295600]
        #   resolution_ns: 100
        # }
        # jitter = {
        #   count: 3238279
        #   avg_ns: 15
        #   max_ns: 295100
        #   total_ns: 48577800
        #   resolution_ns: 100
        # }
        print()
        print('min ns =', stats.latency.min_ns)
        print('avg ns =', stats.latency.avg_ns)
        # these two should be approximately equal - within about 1%
        a = stats.latency.avg_ns * stats.latency.count
        b = stats.latency.total_ns
        diff = abs(a - b) / float(b) * 100.0
        self.assertLessEqual(diff, 1.0)


suite = unittest.TestLoader().loadTestsFromTestCase(BessTimestampTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
