# Copyright (c) 2016-2019, Nefeli Networks, Inc.
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


class BessQueueOccupancyTest(BessModuleTestCase):
    def _send_packets(self, q):
        eth = scapy.Ether(src='02:1e:67:9f:4d:ae', dst='06:16:3e:1b:72:32')
        ip = scapy.IP(src='172.16.0.2', dst='8.8.8.8')
        tcp = scapy.TCP(sport=52428, dport=80)
        l7 = 'helloworld'
        pkt = eth / ip / tcp / l7

        pkts = [pkt] * 100
        _ = self.run_module(q, 0, pkts, [0])
        return len(pkts)

    def test_hist_enable(self):
        q = Queue(size=1024, track_occupancy=True)
        sent = self._send_packets(q)
        resp = q.get_status()
        self.assertEqual(resp.enqueued, sent)
        self.assertEqual(resp.dequeued, sent)
        self.assertEqual(resp.occupancy_summary.count, sent)
        
    def test_hist_disable(self):
        q = Queue(size=1024, track_occupancy=False)
        sent = self._send_packets(q)
        resp = q.get_status()
        self.assertEqual(resp.enqueued, sent)
        self.assertEqual(resp.dequeued, sent)
        self.assertEqual(resp.occupancy_summary.count, 0)

    def test_hist_size(self):
        q = Queue(size=1024, track_occupancy=True)
        resp = q.get_status()
        self.assertEqual(resp.size, 1024)
        self.assertEqual(resp.occupancy_summary.num_buckets, 32)
        self.assertEqual(resp.occupancy_summary.bucket_width, 32)

        q.set_size(size=2048)
        resp = q.get_status()
        self.assertEqual(resp.size, 2048)
        self.assertEqual(resp.occupancy_summary.num_buckets, 32)
        self.assertEqual(resp.occupancy_summary.bucket_width, 64)

        q = Queue(size=1024, track_occupancy=True, occupancy_hist_buckets=64)
        resp = q.get_status()
        self.assertEqual(resp.size, 1024)
        self.assertEqual(resp.occupancy_summary.num_buckets, 64)
        self.assertEqual(resp.occupancy_summary.bucket_width, 16)

    def test_hist_summary(self):
        q = Queue(size=1024, track_occupancy=True)
        sent = self._send_packets(q)

        resp = q.get_status(occupancy_percentiles=[0.5, 0.9, 0.99])
        self.assertEqual(resp.occupancy_summary.count, 100)
        self.assertEqual(len(resp.occupancy_summary.percentile_values), 3)

        resp = q.get_status(occupancy_percentiles=[0, 0.5, 0.9, 0.99])
        self.assertEqual(resp.occupancy_summary.count, 100)
        self.assertEqual(len(resp.occupancy_summary.percentile_values), 4)

        resp = q.get_status(clear_hist=True)
        self.assertEqual(resp.occupancy_summary.count, 100)

        resp = q.get_status()
        self.assertEqual(resp.occupancy_summary.count, 0)


suite = unittest.TestLoader().loadTestsFromTestCase(BessQueueOccupancyTest)
results = unittest.TextTestRunner(verbosity=2, stream=sys.stdout).run(suite)

if results.failures or results.errors:
    sys.exit(1)
