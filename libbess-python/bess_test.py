import unittest
import bess_msg_pb2 as bess_msg
import error_pb2 as error_msg
import service_pb2
import grpc
import bess
import time
from concurrent import futures


class TestServiceImpl(service_pb2.BESSControlServicer):
    def __init__(self):
        pass

    def KillBess(self, request, context):
        return bess_msg.EmptyResponse()

    def CreatePort(self, request, context):
        response = bess_msg.CreatePortResponse()
        response.name = request.name
        return response

    def ModuleCommand(self, request, context):
        response = bess_msg.ModuleCommandResponse()
        return response

    def ListModules(self, request, context):
        response = bess_msg.ListModulesResponse()
        return response


class TestBESS(unittest.TestCase):
    def setUp(self):
        self.server = grpc.server(futures.ThreadPoolExecutor(max_workers=2))
        service_pb2.add_BESSControlServicer_to_server(
            TestServiceImpl(),
            self.server)
        self.server.add_insecure_port('[::]:%d' % bess.BESS.DEF_PORT)
        self.server.start()

    def tearDown(self):
        self.server.stop(0)

    def test_connect(self):
        client = bess.BESS()
        client.connect()
        time.sleep(1)
        self.assertEqual(True, client.is_connected())

        client.disconnect()
        time.sleep(1)
        self.assertEqual(False, client.is_connected())

    def test_kill(self):
        client = bess.BESS()
        client.connect()

        response = client.kill(block=False)
        self.assertEqual(0, response.error.err)

    def test_list_modules(self):
        client = bess.BESS()
        client.connect()

        response = client.list_modules()
        self.assertEqual(0, response.error.err)

    def test_create_port(self):
        client = bess.BESS()
        client.connect()

        response = client.create_port('PCAPPort', 'p0', {'dev': 'rnd'})
        self.assertEqual(0, response.error.err)
        self.assertEqual('p0', response.name)

        response = client.create_port('PMDPort', 'p0', {
            'loopback': True,
            'port_id': 14325,
            'pci': 'akshdkashf'})
        self.assertEqual(0, response.error.err)
        self.assertEqual('p0', response.name)

        response = client.create_port('UnixSocketPort', 'p0',
                                      {'path': '/ajksd/dd'})
        self.assertEqual(0, response.error.err)
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
        self.assertEqual(0, response.error.err)
        self.assertEqual('p0', response.name)

    def test_run_module_command(self):
        client = bess.BESS()
        client.connect()

        response = client.run_module_command('m1',
                                             'add',
                                             'ExactMatchCommandAddArg',
                                             {'gate': 0,
                                              'fields': ['\x11', '\x22']})
        self.assertEqual(0, response.error.err)
