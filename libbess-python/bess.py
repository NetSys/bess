import errno
import grpc
import inspect
import os
import pprint
import socket
import struct
import sys
import threading
import time

import service_pb2
import protobuf_to_dict as pb_conv

import module_msg
import bess_msg_pb2 as bess_msg
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
            return 'errno: %d (%s: %s), %s, details: %s' % (
                self.err, err_code, os.strerror(self.err), self.errmsg,
                repr(self.details))

    # abnormal RPC failure
    class RPCError(Exception):
        pass

    # errors from this class itself
    class APIError(Exception):
        pass

    DEF_PORT = 10514
    BROKEN_CHANNEL = "AbnormalDisconnection"

    def __init__(self):
        self.debug = False
        self.stub = None
        self.channel = None
        self.peer = None

        self.status = None

    def is_connected(self):
        return (self.status == grpc.ChannelConnectivity.READY)

    def is_connection_broken(self):
        return self.status == self.BROKEN_CHANNEL

    def _update_status(self, connectivity):
        if self.debug:
            print "Channel status: {} -> {}".format(self.status, connectivity)

        # do not update status if previous disconnection is not reported yet
        if self.is_connection_broken():
            return

        if self.status == grpc.ChannelConnectivity.READY and \
                connectivity == grpc.ChannelConnectivity.TRANSIENT_FAILURE:
            self.status = self.BROKEN_CHANNEL
        else:
            self.status = connectivity

    def connect(self, host='localhost', port=DEF_PORT):
        if self.is_connected():
            raise self.APIError('Already connected')

        self.status = None
        self.peer = (host, port)
        self.channel = grpc.insecure_channel('%s:%d' % (host, port))
        self.channel.subscribe(self._update_status, try_to_connect=True)
        self.stub = service_pb2.BESSControlStub(self.channel)

        while not self.is_connected():
            if self.status in [grpc.ChannelConnectivity.TRANSIENT_FAILURE,
                               grpc.ChannelConnectivity.SHUTDOWN,
                               self.BROKEN_CHANNEL]:
                self.disconnect()
                raise self.APIError('Connection to %s:%d failed' % (host, port))
            time.sleep(0.1)

    # returns no error if already disconnected
    def disconnect(self):
        try:
            if self.is_connected():
                self.channel.unsubscribe(self._update_status)
        finally:
            self.status = None
            self.stub = None
            self.channel = None
            self.peer = None

    def set_debug(self, flag):
        self.debug = flag

    def _request(self, name, request=None):
        if not self.is_connected():
            if self.is_connection_broken():
                # The channel got abnormally (and asynchronously) disconnected,
                # but RPCError has not been raised yet?
                raise self.RPCError('Broken RPC channel')
            else:
                raise self.APIError('BESS daemon not connected')

        req_fn = getattr(self.stub, name)
        if request is None:
            request = bess_msg.EmptyRequest()

        if self.debug:
            print '====',  req_fn._method
            req = pb_conv.protobuf_to_dict(request)
            print '--->', type(request).__name__
            if req:
                pprint.pprint(req)

        try:
            response = req_fn(request)
        except grpc._channel._Rendezvous as e:
            raise self.RPCError(str(e))

        if self.debug:
            print '<---', type(response).__name__
            res = pb_conv.protobuf_to_dict(response)
            if res:
                pprint.pprint(res)

        if response.error.err != 0:
            err = response.error.err
            errmsg = response.error.errmsg
            if errmsg == '':
                errmsg = '(error message is not given)'
            details = response.error.details
            if details == '':
                details = None
            raise self.Error(err, errmsg, details)

        return response

    def kill(self, block=True):
        try:
            response = self._request('KillBess')
        except grpc._channel._Rendezvous:
            pass

        if block:
            while self.is_connected():
                time.sleep(0.1)

        self.disconnect()
        return response

    def reset_all(self):
        return self._request('ResetAll')

    def pause_all(self):
        return self._request('PauseAll')

    def resume_all(self):
        return self._request('ResumeAll')

    def list_drivers(self):
        return self._request('ListDrivers')

    def get_driver_info(self, name):
        request = bess_msg.GetDriverInfoRequest()
        request.driver_name = name
        return self._request('GetDriverInfo', request)

    def reset_ports(self):
        return self._request('ResetPorts')

    def list_ports(self):
        return self._request('ListPorts')

    def create_port(self, driver, name=None, arg=None):
        arg = arg or {}

        request = bess_msg.CreatePortRequest()
        request.name = name or ''
        request.driver = driver
        request.num_inc_q = arg.pop('num_inc_q', 0)
        request.num_out_q = arg.pop('num_out_q', 0)
        request.size_inc_q = arg.pop('size_inc_q', 0)
        request.size_out_q = arg.pop('size_out_q', 0)
        request.mac_addr = arg.pop('mac_addr', '')

        message_type = getattr(port_msg, driver + 'Arg', bess_msg.EmptyArg)
        arg_msg = pb_conv.dict_to_protobuf(message_type, arg)
        request.arg.Pack(arg_msg)

        return self._request('CreatePort', request)

    def destroy_port(self, name):
        request = bess_msg.DestroyPortRequest()
        request.name = name
        return self._request('DestroyPort', request)

    def get_port_stats(self, name):
        request = bess_msg.GetPortStatsRequest()
        request.name = name
        return self._request('GetPortStats', request)

    def get_link_status(self, name):
        request = bess_msg.GetLinkStatusRequest()
        request.name = name
        return self._request('GetLinkStatus', request)

    def import_mclass(self, path):
        request = bess_msg.ImportMclassRequest()
        request.path = path
        return self._request('ImportMclass', request)

    def list_mclasses(self):
        return self._request('ListMclass')

    def list_modules(self):
        return self._request('ListModules')

    def get_mclass_info(self, name):
        request = bess_msg.GetMclassInfoRequest()
        request.name = name
        return self._request('GetMclassInfo', request)

    def reset_modules(self):
        return self._request('ResetModules')

    def create_module(self, mclass, name=None, arg=None):
        arg = arg or {}

        request = bess_msg.CreateModuleRequest()
        request.name = name or ''
        request.mclass = mclass

        message_type = getattr(module_msg, mclass + 'Arg', bess_msg.EmptyArg)
        arg_msg = pb_conv.dict_to_protobuf(message_type, arg)
        request.arg.Pack(arg_msg)

        return self._request('CreateModule', request)

    def destroy_module(self, name):
        request = bess_msg.DestroyModuleRequest()
        request.name = name
        return self._request('DestroyModule', request)

    def get_module_info(self, name):
        request = bess_msg.GetModuleInfoRequest()
        request.name = name
        return self._request('GetModuleInfo', request)

    def connect_modules(self, m1, m2, ogate=0, igate=0):
        request = bess_msg.ConnectModulesRequest()
        request.m1 = m1
        request.m2 = m2
        request.ogate = ogate
        request.igate = igate
        return self._request('ConnectModules', request)

    def disconnect_modules(self, name, ogate=0):
        request = bess_msg.DisconnectModulesRequest()
        request.name = name
        request.ogate = ogate
        return self._request('DisconnectModules', request)

    def run_module_command(self, name, cmd, arg_type, arg):
        request = bess_msg.ModuleCommandRequest()
        request.name = name
        request.cmd = cmd

        message_type = getattr(module_msg, arg_type, bess_msg.EmptyArg)
        arg_msg = pb_conv.dict_to_protobuf(message_type, arg)

        request.arg.Pack(arg_msg)

        response = self._request('ModuleCommand', request)
        if response.HasField('other'):
            response_type_str = response.other.type_url.split('.')[-1]
            response_type = getattr(module_msg, response_type_str,
                                    bess_msg.EmptyArg)
            result = response_type()
            response.other.Unpack(result)
            return result
        else:
            return response

    def enable_tcpdump(self, fifo, m, direction='out', gate=0):
        request = bess_msg.EnableTcpdumpRequest()
        request.name = m
        request.is_igate = (direction == 'in')
        request.gate = gate
        request.fifo = fifo
        return self._request('EnableTcpdump', request)

    def disable_tcpdump(self, m, direction='out', gate=0):
        request = bess_msg.DisableTcpdumpRequest()
        request.name = m
        request.is_igate = (direction == 'in')
        request.gate = gate
        return self._request('DisableTcpdump', request)

    def enable_track(self, m, direction='out', gate=None):
        request = bess_msg.EnableTrackRequest()
        request.name = m
        if gate is None:
            request.use_gate = False
        else:
            request.use_gate = True
            request.gate = gate
        request.is_igate = (direction == 'in')
        return self._request('EnableTrack', request)

    def disable_track(self, m, direction='out', gate=None):
        request = bess_msg.DisableTrackRequest()
        request.name = m
        if gate is None:
            request.use_gate = False
        else:
            request.use_gate = True
            request.gate = gate
        request.is_igate = (direction == 'in')
        return self._request('DisableTrack', request)

    def list_workers(self):
        return self._request('ListWorkers')

    def add_worker(self, wid, core):
        request = bess_msg.AddWorkerRequest()
        request.wid = wid
        request.core = core
        return self._request('AddWorker', request)

    def destroy_worker(self, wid):
        request = bess_msg.DestroyWorkerRequest()
        request.wid = wid
        return self._request('DestroyWorker', request)

    def attach_task(self, m, tid=0, tc=None, wid=None):
        if (tc is None) == (wid is None):
            raise self.APIError('You should specify either "tc" or "wid"'
                                ', but not both')

        request = bess_msg.AttachTaskRequest()
        request.name = m
        request.taskid = tid

        if tc is not None:
            request.tc = tc
        else:
            request.wid = wid

        return self._request('AttachTask', request)

    def list_tcs(self, wid=-1):
        request = bess_msg.ListTcsRequest()
        request.wid = wid

        return self._request('ListTcs', request)

    def add_tc(self, name, wid=0, parent='', policy='priority', resource=None,
               priority=None, share=None, limit=None, max_burst=None):
        request = bess_msg.AddTcRequest()
        class_ = getattr(request, 'class')
        class_.parent = parent
        class_.name = name
        class_.wid = wid
        class_.policy = policy

        if priority is not None:
            class_.priority = priority

        if share is not None:
            class_.share = share

        if resource is not None:
            class_.resource = resource

        if limit:
            for k in limit:
                class_.limit[k] = limit[k]

        if max_burst:
            for k in max_burst:
                class_.max_burst[k] = max_burst[k]

        return self._request('AddTc', request)

    def update_tc(self, name, resource=None, limit=None, max_burst=None):
        request = bess_msg.UpdateTcRequest()
        class_ = getattr(request, 'class')
        class_.name = name
        class_.resource = resource

        if limit:
            for k in limit:
                class_.limit[k] = limit[k]

        if max_burst:
            for k in max_burst:
                class_.max_burst[k] = max_burst[k]

        return self._request('UpdateTc', request)

    def get_tc_stats(self, name):
        request = bess_msg.GetTcStatsRequest()
        request.name = name
        return self._request('GetTcStats', request)
