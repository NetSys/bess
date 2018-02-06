# Copyright (c) 2014-2016, The Regents of the University of California.
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

from __future__ import absolute_import
import unittest
import grpc
import time
from concurrent import futures

from . import bess
from .builtin_pb import bess_msg_pb2 as bess_msg
from .builtin_pb import service_pb2


class DummyServiceImpl(service_pb2.BESSControlServicer):

    def __init__(self):
        pass

    def KillBess(self, request, context):
        return bess_msg.EmptyResponse()

    def CreatePort(self, request, context):
        response = bess_msg.CreatePortResponse()
        response.name = request.name
        return response

    def ModuleCommand(self, request, context):
        response = bess_msg.CommandResponse()
        return response

    def ListModules(self, request, context):
        response = bess_msg.ListModulesResponse()
        return response


class TestBESS(unittest.TestCase):
    # Do not use BESS.DEF_PORT (== 10514), as it might be being used by
    # a real bessd process.
    PORT = 19876
    GRPC_URL = 'localhost:' + str(PORT)

    @classmethod
    def setUpClass(cls):
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=2))
        service_pb2.add_BESSControlServicer_to_server(
            DummyServiceImpl(),
            server)
        server.add_insecure_port('[::]:%d' % cls.PORT)
        server.start()
        cls.server = server

    @classmethod
    def tearDownClass(cls):
        future = cls.server.stop(0)
        future.wait()

    def test_connect(self):
        client = bess.BESS()
        client.connect(grpc_url=self.GRPC_URL)
        time.sleep(0.1)
        self.assertEqual(True, client.is_connected())

        client.disconnect()
        time.sleep(0.1)
        self.assertEqual(False, client.is_connected())

    def test_kill(self):
        client = bess.BESS()
        client.connect(grpc_url=self.GRPC_URL)

        response = client.kill(block=False)
        self.assertEqual(0, response.error.code)

    def test_list_modules(self):
        client = bess.BESS()
        client.connect(grpc_url=self.GRPC_URL)

        response = client.list_modules()
        self.assertEqual(0, response.error.code)

    def test_create_port(self):
        client = bess.BESS()
        client.connect(grpc_url=self.GRPC_URL)

        response = client.create_port('PCAPPort', 'p0', {'dev': 'rnd'})
        self.assertEqual(0, response.error.code)
        self.assertEqual('p0', response.name)

        response = client.create_port('PMDPort', 'p0', {
            'loopback': True,
            'port_id': 14325,
            'pci': 'akshdkashf'})
        self.assertEqual(0, response.error.code)
        self.assertEqual('p0', response.name)

        response = client.create_port('UnixSocketPort', 'p0',
                                      {'path': '/ajksd/dd'})
        self.assertEqual(0, response.error.code)
        self.assertEqual('p0', response.name)

        response = client.create_port('VPort', 'p0', {
            'ifname': 'veth0',
            'container_pid': 23124,
            'rxq_cpus': [1, 2, 3],
            'tx_tci': 123,
            'tx_outer_tci': 123,
            'loopback': False,
            'ip_addrs': ['1.2.3.4', '255.254.253.252']
        })
        self.assertEqual(0, response.error.code)
        self.assertEqual('p0', response.name)

    def test_run_module_command(self):
        client = bess.BESS()
        client.connect(grpc_url=self.GRPC_URL)

        response = client.run_module_command('m1',
                                             'add',
                                             'ExactMatchCommandAddArg',
                                             {'gate': 0,
                                                 'fields': [{'value_bin': b'\x11'}, {'value_bin': b'\x22'}]})
        self.assertEqual(0, response.error.code)
