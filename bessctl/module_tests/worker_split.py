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

import multiprocessing as mp
from test_utils import *


class BessWorkerSplitTest(BessModuleTestCase):

    def test_worker_split_default(self):
        NUM_WORKERS = 2

        for wid in range(NUM_WORKERS):
            for i in range(NUM_WORKERS):
                bess.add_worker(wid=i, core=i)

            src = Source()
            ws = WorkerSplit()
            src -> ws

            for i in range(NUM_WORKERS):
                ws:i -> Sink()

            src.attach_task(wid=wid)

            bess.resume_all()
            time.sleep(1)
            bess.pause_all()

            # packets should flow onto only one output gate...
            ogates = bess.get_module_info(ws.name).ogates
            for ogate in ogates:
                if ogate.ogate == wid:
                    self.assertGreater(ogate.pkts, 0)
                else:
                    self.assertEquals(ogate.pkts, 0)

            bess.reset_all()

    def test_worker_split_fancy(self):
        NUM_WORKERS = mp.cpu_count()

        gates = dict()
        for i in range(NUM_WORKERS):
            gates[i] = i & 1

        for i in range(NUM_WORKERS):
            bess.add_worker(wid=i, core=i)
            bess.add_tc('rl_{}'.format(i), policy='rate_limit', wid=i,
                        resource='count', limit={'count': 1})
        bess.pause_all()

        ws::WorkerSplit(worker_gates=gates)
        ws:0 -> sink0::Sink()
        ws:1 -> sink1::Sink()

        srcs = [Source() for _ in range(NUM_WORKERS)]
        for i in range(NUM_WORKERS):
            srcs[i].attach_task(parent='rl_{}'.format(i))
            srcs[i] -> ws

        bess.resume_all()
        time.sleep(3)
        bess.pause_all()

        odd_pkts = 0
        even_pkts = 0
        for i in range(NUM_WORKERS):
            pkts = bess.get_module_info(srcs[i].name).ogates[0].pkts
            if i % 2 == 0:
                even_pkts += pkts
            else:
                odd_pkts += pkts

        ws_ogates = bess.get_module_info(ws.name).ogates
        for ogate in ws_ogates:
            if ogate.ogate == 0:
                self.assertEquals(ogate.pkts, odd_pkts)
            elif ogate.ogate == 1:
                self.assertEquals(ogate.pkts, odd_pkts)

        bess.reset_all()

suite = unittest.TestLoader().loadTestsFromTestCase(BessWorkerSplitTest)
results = unittest.TextTestRunner(verbosity=2).run(suite)

if results.failures or results.errors:
    sys.exit(1)
