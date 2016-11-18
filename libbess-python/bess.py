import socket
import struct
import errno
import sys
import os
import inspect
import time
import grpc

import service_pb2
import proto_conv
import bess_msg_pb2 as bess_msg
import module_msg_pb2 as module_msg
import port_msg_pb2 as port_msg

class BESS(object):

    # errors from BESS daemon
    class Error(Exception):
        def __init__(self, err, errmsg, details):
            self.err = err
            self.errmsg = errmsg
            self.details = details

        def __str__(self):
            if self.err in errno.errorcode:
                err_code = errno.errorcode[self.err]
            else:
                err_code = '<unknown>'
            return 'errno: %d (%s: %s), %s, details: %s' % \
                    (self.err, err_code, os.strerror(self.err),
                            self.errmsg, repr(self.details))

    # errors from this class itself
    class APIError(Exception):
        pass

    DEF_PORT = 10514

    def __init__(self):
        self.debug = False
        self.stub = None
        self.channel = None
        self.status = None
        self.peer = None

    def is_connected(self):
        return self.status == grpc.ChannelConnectivity.READY

    def _update_status(self, connectivity):
        self.status = connectivity

    def connect(self, host='localhost', port=DEF_PORT):
        if self.is_connected():
            raise self.APIError('Already connected')
        self.peer = (host, port)
        self.channel = grpc.insecure_channel('%s:%d' % (host, port))
        self.channel.subscribe(self._update_status, try_to_connect=True)
        self.stub = service_pb2.BESSControlStub(self.channel)
        time.sleep(0.1) # Hack: Wait for gRPC to connect

    def disconnect(self):
        if self.is_connected():
            self.channel.unsubscribe(self._update_status)
            self.status = None
            self.stub = None
            self.channel = None
            self.peer = None

    def set_debug(self, flag):
        self.debug = flag

    def _request(self, req_fn, request=None):
        if request is None: request = bess_msg.EmptyRequest()
        response = req_fn(request)
        if response.error.err != 0:
            err = response.error.err
            errmsg = response.error.errmsg
            if errmsg == '': errmsg = '(error message is not given)'
            details = response.error.details
            if details == '': details = None
            raise self.Error(err, errmsg, details)
        return response

    def kill(self):
        try:
            self._request(self.stub.KillBess)
        except grpc._channel._Rendezvous:
            pass

    def reset_all(self):
        return self._request(self.stub.ResetAll)

    def pause_all(self):
        return self._request(self.stub.PauseAll)

    def resume_all(self):
        return self._request(self.stub.ResumeAll)

    def list_drivers(self):
        return self._request(self.stub.ListDrivers)

    def get_driver_info(self, name):
        request = bess_msg.GetDriverInfoRequest()
        request.driver_name = name
        return self._request(self.stub.GetDriverInfo, request)

    def reset_ports(self):
        return self._request(self.stub.ResetPorts)

    def list_ports(self):
        return self._request(self.stub.ListPorts)

    def create_port(self, driver, name, arg):
        kv = {
            'name': name,
            'driver': driver,
            'num_inc_q': arg['num_inc_q'] if 'num_inc_q' in arg else 0,
            'num_out_q': arg['num_out_q'] if 'num_out_q' in arg else 0,
            'size_inc_q': arg['size_inc_q'] if 'size_inc_q' in arg else 0,
            'size_out_q': arg['size_out_q'] if 'size_out_q' in arg else 0,
            'mac_addr': arg['mac_addr'] if 'mac_addr' in arg else '',
        }

        request = proto_conv.dict_to_protobuf(kv, bess_msg.CreatePortRequest)
        message_map = {
            'PCAPPort': port_msg.PCAPPortArg,
            'PMDPort': port_msg.PMDPortArg,
            'UnixSocketPort': port_msg.UnixSocketPortArg,
            'ZeroCopyVPort': port_msg.ZeroCopyVPortArg,
            'VPort': port_msg.VPortArg,
        }
        arg_msg = proto_conv.dict_to_protobuf(arg, message_map[driver])
        request.arg.Pack(arg_msg)

        return self._request(self.stub.CreatePort, request)

    def destroy_port(self, name):
        request = bess_msg.DestroyPortRequest()
        request.name = name
        return self._request(self.stub.DestroyPort, request)

    def get_port_stats(self, name):
        request = bess_msg.GetPortStatsRequest()
        request.name = name
        return self._request(self.stub.GetPortStats, request)

    def list_mclasses(self):
        return self._request(self.stub.ListMclass)

    def list_modules(self):
        return self._request(self.stub.ListModules)

    def get_mclass_info(self, name):
        request = bess_msg.GetMclassInfoRequest()
        request.name = name
        return self._request(self.stub.GetMclassInfo, request)

    def reset_modules(self):
        return self._request(self.stub.ResetModules)

    def create_module(self, mclass, name, arg):
        kv = {
            'name': name,
            'mclass': mclass,
        }
        request = proto_conv.dict_to_protobuf(kv, bess_msg.CreateModuleRequest)
        message_map = {
            'BPF': module_msg.BPFArg,
            'Buffer': bess_msg.EmptyArg,
            'Bypass': bess_msg.EmptyArg,
            'Dump': module_msg.DumpArg,
            'EtherEncap': bess_msg.EmptyArg,
            'ExactMatch': module_msg.ExactMatchArg,
            'FlowGen': module_msg.FlowGenArg,
            'GenericDecap': module_msg.GenericDecapArg,
            'GenericEncap': module_msg.GenericEncapArg,
            'HashLB': module_msg.HashLBArg,
            'IPEncap': bess_msg.EmptyArg,
            'IPLookup': bess_msg.EmptyArg,
            'L2Forward': module_msg.L2ForwardArg,
            'MACSwap': bess_msg.EmptyArg,
            'Measure': module_msg.MeasureArg,
            'Merge': bess_msg.EmptyArg,
            'MetadataTest': module_msg.MetadataTestArg,
            'NoOP': bess_msg.EmptyArg,
            'PortInc': module_msg.PortIncArg,
            'PortOut': module_msg.PortOutArg,
            'QueueInc': module_msg.QueueIncArg,
            'QueueOut': module_msg.QueueOutArg,
            'Queue': module_msg.QueueArg,
            'RandomUpdate': module_msg.RandomUpdateArg,
            'Rewrite': module_msg.RewriteArg,
            'RoundRobin': module_msg.RoundRobinArg,
            'SetMetadata': module_msg.SetMetadataArg,
            'Sink': bess_msg.EmptyArg,
            'Source': module_msg.SourceArg,
            'Split': module_msg.SplitArg,
            'Timestamp': bess_msg.EmptyArg,
            'Update': module_msg.UpdateArg,
            'VLANPop': bess_msg.EmptyArg,
            'VLANPush': module_msg.VLANPushArg,
            'VLANSplit': bess_msg.EmptyArg,
            'VXLANDecap': bess_msg.EmptyArg,
            'VXLANEncap': module_msg.VXLANEncapArg,
            'WildcardMatch': module_msg.WildcardMatchArg,
        }
        arg_msg = proto_conv.dict_to_protobuf(arg, message_map[mclass])
        request.arg.Pack(arg_msg)
        return self._request(self.stub.CreateModule, request)

    def destroy_module(self, name):
        request = bess_msg.DestroyModuleRequest()
        request.name = name
        return self._request(self.stub.DestroyModule, request)

    def get_module_info(self, name):
        request = bess_msg.GetModuleInfoRequest()
        request.name = name
        return self._request(self.stub.GetModuleInfo, request)

    def connect_modules(self, m1, m2, ogate=0, igate=0):
        request = bess_msg.ConnectModulesRequest()
        request.m1 = m1
        request.m2 = m2
        request.ogate = ogate
        request.igate = igate
        return self._request(self.stub.ConnectModules, request)

    def disconnect_modules(self, name, ogate = 0):
        request = bess_msg.DisconnectModulesRequest()
        request.name = name
        request.ogate = ogate
        return self._request(self.stub.DisconnectModules, request)

    def run_module_command(self, name, cmd, arg_type, arg):
        request = bess_msg.ModuleCommandRequest()
        request.name = name
        request.cmd = cmd

        all_classes = inspect.getmembers(module_msg, lambda c: inspect.isclass(c))
        arg_classes = dict(all_classes)
        arg_classes['EmptyArg'] = bess_msg.EmptyArg

        arg_msg = proto_conv.dict_to_protobuf(arg, arg_classes[arg_type])
        request.arg.Pack(arg_msg)

        response = self._request(self.stub.ModuleCommand, request)
        if response.HasField('other'):
            type_str = response.other.type_url.split('.')[-1]
            type_class = arg_classes[type_str]
            result = type_class()
            response.other.Unpack(result)
        else:
            return response

    def enable_tcpdump(self, fifo, m, direction='out', gate=0):
        request = bess_msg.EnableTcpdumpRequest()
        request.name = m
        request.is_igate = (direction == 'in')
        request.gate = gate
        request.fifo = fifo
        return self._request(self.stub.EnableTcpdump, request)

    def disable_tcpdump(self, m, direction='out', gate=0):
        request = bess_msg.DisableTcpdumpRequest()
        request.name = m
        request.is_igate = (direction == 'in')
        request.gate = gate
        return self._request(self.stub.DisableTcpdump, request)

    def enable_track(self, m, direction='out', gate=None):
        args = {'name': m, 'gate_idx': gate, 'is_igate': int(direction == 'in')}
        return self._request_bess('enable_track', args)

    def disable_track(self, m, direction='out', gate=None):
        args = {'name': m, 'gate_idx': gate, 'is_igate': int(direction == 'in')}
        return self._request_bess('disable_track', args)

    def list_workers(self):
        return self._request(self.stub.ListWorkers)

    def add_worker(self, wid, core):
        request = bess_msg.AddWorkerRequest()
        request.wid = wid
        request.core = core
        return self._request(self.stub.AddWorker, request)

    def attach_task(self, m, tid=0, tc=None, wid=None):
        if (tc is None) == (wid is None):
            raise self.APIError('You should specify either "tc" or "wid"' \
                    ', but not both')

        request = bess_msg.AttachTaskRequest()
        request.name = m
        request.taskid = tid

        if tc is not None:
            request.tc = tc
        else:
            request.wid = wid

        return self._request(self.stub.AttachTask, request)

    def list_tcs(self, wid = None):
        request = bess_msg.ListTcsRequest()
        if wid is not None:
            request.wid = wid

        return self._request(self.stub.ListTcs, request)

    def add_tc(self, name, wid=0, priority=0, limit=None, max_burst=None):
        request = bess_msg.AddTcRequest()
        class_ = getattr(request, 'class')
        class_.name = name
        class_.wid = wid
        class_.priority = priority
        if limit:
            class_.limit.schedules = limit['schedules'] if 'schedules' in limit else 0
            class_.limit.cycles = limit['cycles'] if 'cycles' in limit else 0
            class_.limit.packets = limit['packets'] if 'packets' in limit else 0
            class_.limit.bits = limit['bits'] if 'bits' in limit else 0

        if max_burst:
            class_.max_burst.schedules = max_burst['schedules'] if 'schedules' in max_burst else 0
            class_.max_burst.cycles = max_burst['cycles'] if 'cycles' in max_burst else 0
            class_.max_burst.packets = max_burst['packets'] if 'packets' in max_burst else 0
            class_.max_burst.bits = max_burst['bits'] if 'bits' in max_burst else 0

        return self._request(self.stub.AddTc, request)

    def get_tc_stats(self, name):
        request = bess_msg.GetTcStatsRequest()
        request.name = name
        return self._request(self.stub.GetTcStats, request)
