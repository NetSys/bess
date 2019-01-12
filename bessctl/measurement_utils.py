# Copyright (c) 2017, Nefeli Networks, Inc.
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

import copy
import os
import sys
import time

try:
    this_dir = os.path.dirname(os.path.realpath(__file__))
    bessctl = os.path.join(this_dir, 'bessctl')
    sys.path.insert(1, os.path.join(this_dir, '../../../'))
    from pybess.bess import *
except ImportError:
    print('Cannot import the API module (pybess)', file=sys.stderr)
    raise


def get_local_bess_handle():
    bess = BESS()
    try:
        bess.connect()
    except BESS.RPCError:
        raise Exception('BESS is not running')
    return bess


class PortStats(object):

    """
    Stores PMDPort stats and optional Measure module output and exports some
    acceessors for convenience.
    """

    def __init__(self, throughput, rtt, jitter):
        self.throughput = throughput
        self.rtt = rtt
        self.jitter = jitter

    def pkts_in(self):
        return self.throughput.inc.packets

    def pkts_out(self):
        return self.throughput.out.packets

    def drops_in(self):
        return self.throughput.inc.dropped

    def drops_out(self):
        return self.throughput.out.dropped

    def bytes_in(self):
        return self.throughput.inc.bytes

    def bytes_out(self):
        return self.throughput.out.bytes

    def rtt(self, percentile=None):
        """
        Returns the dictionary of all RTT value if `percentile` is none.
        Otherwise returns the `percentile`th percentile RTT.
        """
        if self.rtt is None or percentile is None:
            return self.rtt
        return self.rtt[percentile]

    def jitter(self, percentile=None):
        """
        Returns the dictionary of all jitter value if `percentile` is none.
        Otherwise returns the `percentile`th percentile jitter.
        """
        if self.jitter is None or percentile is None:
            return self.jitter
        return self.jitter[percentile]

    def __str__(self):
        def format_data(prefix, data, dst):
            keys = sorted(data.keys())
            for k in keys:
                dst.append(data[k] / 1e3)
            return ', '.join(['{}{}: {{:.3f}} us'.format(prefix, k) for k in keys])

        fmt = '[in / out] pkts: {:.3f}M / {:.3f}M, drops: {:.3f}M / {:.3f}M, bytes: {} / {}'
        rtt_data = []
        if self.rtt is not None:
            fmt += '\n' + format_data('rtt', self.rtt, rtt_data)
            fmt += '\n' + format_data('jitter', self.jitter, rtt_data)

        return fmt.format(
            self.pkts_in() / 1e6, self.pkts_out() / 1e6,
            self.drops_in() / 1e6, self.drops_out() / 1e6,
            self.bytes_in(), self.bytes_out(),
            *rtt_data)

    def __repr__(self):
        return self.__str__()


class PortStatsGenerator(object):

    def __init__(self, bess, tx_port, rx_port, measure=None, rtt_percentiles=list(), rate=False):
        """
        Creates a generator that produces PortStats. When `tx_port` and
        `rx_port` are configured differently, the generated PortStats objects
        will have outbound throughput reported as seen by `tx_port` and inbound
        throughput will be reported as seen by `rx_port`.

        If `rate` is true, generated port stats will be reported as rates
        instead of cummulative values. If `rate` is true and RTT stats are being
        collected, i.e. `measure` is not None, the generated PortStats objects
        will have RTT stats reported as the average of those seen between
        subsequent calls to `next()`.
        """
        self.bess = bess
        self.tx_port = tx_port
        self.rx_port = rx_port
        self.measure = measure
        self.rtt_percentiles = rtt_percentiles
        self.rate = rate
        self.old_throughput = None
        self.old_rtt = None
        self.old_jitter = None
        self.last_check = time.time()

    def __iter__(self):
        return self

    def __next__(self):
        return self.next()

    @staticmethod
    def throughput_delta(old, new, secs):
        delta = copy.copy(old)
        secs = int(secs)
        if secs > 0:
            delta.inc.packets = (new.inc.packets - old.inc.packets) // secs
            delta.inc.dropped = (new.inc.dropped - old.inc.dropped) // secs
            delta.inc.bytes = (new.inc.bytes - old.inc.bytes) // secs
            delta.out.packets = (new.out.packets - old.out.packets) // secs
            delta.out.dropped = (new.out.dropped - old.out.dropped) // secs
            delta.out.bytes = (new.out.bytes - old.out.bytes) // secs
        return delta

    @staticmethod
    def aggregate_rtt(old, new):
        if old is None:
            return new.copy()
        return {k: (old[k] + new[k]) / 2 for k in old}

    def next(self):
        throughput = self.bess.get_port_stats(self.tx_port)
        if self.tx_port != self.rx_port:
            rx_stats = self.bess.get_port_stats(self.rx_port)
            throughput.inc.packets = rx_stats.inc.packets
            throughput.inc.dropped = rx_stats.inc.dropped
            throughput.inc.bytes = rx_stats.inc.bytes

        rtt, jitter = None, None
        if self.measure:
            arg = {'clear': True,
                   'latency_percentiles': self.rtt_percentiles,
                   'jitter_percentiles': self.rtt_percentiles}
            mstats = self.bess.run_module_command(self.measure, 'get_summary',
                                                  'MeasureCommandGetSummaryArg',
                                                  arg)
            rtt = dict()
            jitter = dict()
            for i, p in enumerate(self.rtt_percentiles):
                rtt[p] = mstats.latency.percentile_values_ns[i]
                jitter[p] = mstats.jitter.percentile_values_ns[i]

        if self.rate:
            now = time.time()
            delta_t = now - self.last_check
            if self.old_throughput == None:
                self.old_throughput = copy.copy(throughput)
                self.old_throughput.inc.packets = 0
                self.old_throughput.inc.dropped = 0
                self.old_throughput.inc.bytes = 0
                self.old_throughput.out.packets = 0
                self.old_throughput.out.dropped = 0
                self.old_throughput.out.bytes = 0

            delta_throughput = PortStatsGenerator.throughput_delta(
                self.old_throughput, throughput, delta_t)
            self.old_throughput = throughput
            self.last_check = now

            agg_rtt, agg_jitter = None, None
            if self.measure:
                agg_rtt = PortStatsGenerator.aggregate_rtt(self.old_rtt, rtt)
                agg_jitter = PortStatsGenerator.aggregate_rtt(
                    self.old_jitter, jitter)
                self.old_rtt = rtt
                self.old_jitter = jitter

            return PortStats(delta_throughput, agg_rtt, agg_jitter)

        return PortStats(throughput, rtt, jitter)


class PortConfig(object):

    def __init__(self, name, num_queues=1, pci=None, port_id=None, vdev=None,
                 no_tx=False, no_rx=False):
        """
        Creates a configuraiton for a Port named `name` and configures it with `num_queues` tx and
        rx queues. If `no_tx` is set, no PortOut will be created. If `no_rx` is
        set, no PortInc will be created. If `bess` is None, will attempt to
        create all ports and modules on bessd running on localhost.
        """
        if [pci, port_id, vdev].count(None) != 2:
            raise TypeError(
                'exactly one of `pci`, `port_id` or `vdev` must be specified')

        self.name = name
        self.pci = pci
        self.port_id = port_id
        self.vdev = vdev
        self.no_tx = no_tx
        self.no_rx = no_rx
        self.num_queues = num_queues


class Port(object):

    """
    A thin wrapper around PMDPort for use in MeasurablePort. Keeps track of a
    PMDPort along with a PortInc and a PortOut.
    """

    def __init__(self, conf, bess):
        self.name = conf.name
        self.pmd = None
        self.mac_addr = None
        self.port_inc = None
        self.port_out = None

        if conf.pci is not None:
            self.pmd = bess.create_port('PMDPort', conf.name,
                                        {'pci': conf.pci, 'num_inc_q':
                                            conf.num_queues, 'num_out_q': conf.num_queues})
        elif conf.port_id is not None:
            self.pmd = bess.create_port('PMDPort', conf.name,
                                        {'port_id': conf.port_id, 'num_inc_q':
                                            conf.num_queues, 'num_out_q': conf.num_queues})
        elif conf.vdev is not None:
            self.pmd = bess.create_port('PMDPort', conf.name,
                                        {'vdev': conf.vdev, 'num_inc_q':
                                         conf.num_queues, 'num_out_q': conf.num_queues})

        self.mac_addr = self.pmd.mac_addr

        if not conf.no_rx:
            inc_name = 'port_inc_{}'.format(self.name)
            self.port_inc = bess.create_module(
                'PortInc', inc_name, {'port': self.pmd.name}).name

        if not conf.no_tx:
            out_name = 'port_out_{}'.format(self.name)
            self.port_out = bess.create_module(
                'PortOut', out_name, {'port': self.pmd.name}).name


class MeasureablePort():

    """
    A with optional Timestamp and Measures modules attached
    respectively.
    """

    def __init__(self, tx_port, rx_port, tx_ts_offset=None, rx_ts_offset=None, bess=None):
        """
        Creates a MeasurablePort form two PortConfigs `tx_port` and `rx_port`,
        which may be the same. `tx_port` must be configured with `no_tx=False`
        and `rx_port` must be configured with `no_rx=False`.

        If `tx_ts_offset` and `rx_ts_offset` are both not None, a Measure module
        and Timestamp module will be created and connected to the created
        PortOut and PortInc respectively.

        If `bess` is None, will attempt to create all ports and modules on bessd
        running on localhost.
        """
        self.bess = bess

        if self.bess is None:
            self.bess = get_local_bess_handle()

        self.tx_port = Port(tx_port, self.bess)
        if rx_port.name != tx_port.name:
            self.rx_port = Port(rx_port, self.bess)
        else:
            self.rx_port = self.tx_port
        self.measure_rtt = tx_ts_offset is not None and rx_ts_offset is not None
        self.measure = None

        if self.measure_rtt:
            ts_name = 'timestamp_{}'.format(self.tx_port.name)
            self.timestamp = self.bess.create_module(
                'Timestamp', ts_name, {'offset': tx_ts_offset}).name
            self.bess.connect_modules(
                self.timestamp, self.tx_port.port_out, 0, 0)

            measure_name = 'measure_{}'.format(self.rx_port.name)
            self.measure = self.bess.create_module(
                'Measure', measure_name, {'offset': rx_ts_offset}).name
            self.bess.connect_modules(
                self.rx_port.port_inc, self.measure, 0, 0)

    def connect_rx(self, dst, igate):
        """
        Connect `igate`:`dst` to Measure:0 if measuring RTT, otherwise
        connects directly to PortInc:0.
        """
        if self.measure_rtt:
            self.bess.connect_modules(self.measure, dst.name, 0, igate)
        else:
            self.bess.connect_modules(
                self.rx_port.port_inc, dst.name, 0, igate)

    def connect_tx(self, src, ogate):
        """
        Connect `src`:`ogate` to 0:Timestamp if measuring RTT, otherwise
        connects directly to 0:PortOut.
        """
        if self.measure_rtt:
            self.bess.connect_modules(src.name, self.timestamp, ogate, 0)
        else:
            self.bess.connect_modules(
                src.name, self.tx_port.port_out, ogate, 0)

    def cumulative_stats(self, rtt_percentiles=[0, 25, 50, 99, 100]):
        """
        Returns a PortsStatsGenerator configured to report cummulative PMDPort
        statistics.
        """
        return PortStatsGenerator(self.bess,
                                  self.tx_port.name,
                                  self.rx_port.name,
                                  measure=self.measure,
                                  rtt_percentiles=rtt_percentiles,
                                  rate=False)

    def rate_stats(self, rtt_percentiles=[0, 25, 50, 99, 100]):
        """
        Returns a PortsStatsGenerator configured to report PMDPort statistics as
        rates.
        """
        return PortStatsGenerator(self.bess,
                                  self.tx_port.name,
                                  self.rx_port.name,
                                  measure=self.measure,
                                  rtt_percentiles=rtt_percentiles,
                                  rate=True)
