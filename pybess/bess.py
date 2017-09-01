# Copyright (c) 2014-2017, The Regents of the University of California.
# Copyright (c) 2016-2017, Nefeli Networks, Inc.
# Copyright (c) 2017, Cloudigo.
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
from __future__ import absolute_import

import errno
import grpc
import os
import pprint
import sys
import time

from . import protobuf_to_dict as pb_conv

# pseudo-module multi-importer, used to build module message types
from . import pm_import as _pm

# Ugh: builtin_pb must be on path, as protoc generates python code
# that assumes it can import files in that directory.  With our
# split of builtin and plugin pb files we do not want 'from . import'
# either, so just add 'builtin_pb' to sys.path.
bipath = os.path.abspath(os.path.join(__file__, '..', 'builtin_pb'))
if bipath not in sys.path:
    sys.path.insert(1, bipath)
del bipath

from .builtin_pb import service_pb2
from .builtin_pb import bess_msg_pb2 as bess_msg
from .builtin_pb import module_msg_pb2 as module_msg


def _import_modules(name, subdir):
    """Return a module instance retaining just the *Arg and *Response
    names, built by importing most of the *_msg_pb2.py files in the
    builtin_pb and plugin_pb directories, or a subdirectory of them.
    We skip bess_msg_pb2 for historical reasons.

    We detect any name collisions, e.g., if foo_msg_pb2.py defines
    QuuxArg and bar_msg_pb2.py also defines QuuxArg, we catch that
    error here."""
    def protobufs(subdir):
        "Yield modules (subdir=None) or port drivers (subdir='ports')."
        self_path = os.path.dirname(os.path.relpath(__file__))
        for directory in ('builtin_pb', 'plugin_pb'):
            if subdir:
                dirpath = os.path.join(self_path, directory, subdir)
                prefix = '..{}.{}'.format(directory, subdir)
            else:
                dirpath = os.path.join(self_path, directory)
                prefix = '..{}'.format(directory)
            for filename in os.listdir(dirpath):
                if not filename.endswith('_msg_pb2.py'):
                    continue
                import_name = '{}.{}'.format(prefix, filename[:-3])
                if import_name != '..builtin_pb.bess_msg_pb2':
                    yield import_name

    def keep_name(name):
        return name.endswith('Arg') or name.endswith('Response')

    try:
        mod = _pm.pm_import(name, protobufs(subdir),
                            name_filter=keep_name,
                            package=__name__)
    except _pm.Collisions as err:
        print('internal error:')
        for key in err.collisions:
            print('', key, 'is defined in', ' and '.join(err.collisions[key]))
        raise SystemExit(1)
    return mod

module_pb = _import_modules('module_pb', None)
port_msg = _import_modules('port_msg', 'ports')


def _constraints_to_list(constraint):
    current = 0
    active = []
    while constraint > 0:
        if constraint & 1 == 1:
            active.append(current)
        current += 1
        constraint = constraint >> 1
    return active


class BESS(object):

    class Error(Exception):  # errors from BESS daemon

        def __init__(self, code, errmsg, **kwargs):
            self.code = code
            self.errmsg = errmsg

            # a string->value dict for additional error contexts
            self.info = kwargs

        def __str__(self):
            if self.code in errno.errorcode:
                err_code = errno.errorcode[self.code]
            else:
                err_code = '<unknown>'
            return 'errno=%d (%s: %s), %s' % (
                self.code, err_code, os.strerror(self.code), self.errmsg)

    # abnormal RPC failure
    class RPCError(Exception):
        pass

    # errors from this class itself
    class APIError(Exception):
        pass

    # An error due to an unsatisfied constraint
    class ConstraintError(Exception):
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
            print("Channel status: {} -> {}".format(self.status, connectivity))

        # do not update status if previous disconnection is not reported yet
        if self.is_connection_broken():
            return

        if self.status == grpc.ChannelConnectivity.READY and \
                connectivity == grpc.ChannelConnectivity.TRANSIENT_FAILURE:
            self.status = self.BROKEN_CHANNEL
        else:
            self.status = connectivity

    def connect(self, host='localhost', port=DEF_PORT):
        if self.debug:
            print('Connecting to %s:%d' % (host, port))

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
                raise self.APIError(
                    'Connection to %s:%d failed' % (host, port))
            time.sleep(0.1)

    # returns no error if already disconnected
    def disconnect(self):
        try:
            if self.is_connected():
                if self.debug:
                    print('Disconnecting')
                self.channel.unsubscribe(self._update_status)
        finally:
            self.status = None
            self.stub = None
            self.channel = None
            self.peer = None

    def set_debug(self, flag):
        self.debug = flag

    def _request(self, name, req_pb=None):
        if not self.is_connected():
            if self.is_connection_broken():
                # The channel got abnormally (and asynchronously) disconnected,
                # but RPCError has not been raised yet?
                raise self.RPCError('Broken RPC channel')
            else:
                raise self.APIError('BESS daemon not connected')

        req_fn = getattr(self.stub, name)
        if req_pb is None:
            req_pb = bess_msg.EmptyRequest()

        req_dict = pb_conv.protobuf_to_dict(req_pb)

        if self.debug:
            print('====',  req_fn._method)
            print('--->', type(req_pb).__name__)
            if req_dict:
                pprint.pprint(req_dict)

        try:
            response = req_fn(req_pb)
        except grpc._channel._Rendezvous as e:
            raise self.RPCError(str(e))

        if self.debug:
            print('<---', type(response).__name__)
            res = pb_conv.protobuf_to_dict(response)
            if res:
                pprint.pprint(res)

        if response.error.code != 0:
            code = response.error.code
            errmsg = response.error.errmsg or '(error message is not given)'
            raise self.Error(code, errmsg, query=name, query_arg=req_dict)

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

    def get_version(self):
        return self._request('GetVersion')

    def reset_all(self):
        return self._request('ResetAll')

    def pause_all(self):
        return self._request('PauseAll')

    def pause_worker(self, wid):
        request = bess_msg.PauseWorkerRequest()
        request.wid = wid
        return self._request('PauseWorker', request)

    def check_constraints(self):
        response = self.check_scheduling_constraints()
        error = False
        if len(response.violations) != 0 or len(response.modules) != 0:
            print('Placement violations found')
            for violation in response.violations:
                if violation.constraint != 0:
                    valid = ' '.join(
                        map(str, _constraints_to_list(violation.constraint)))
                    print('name %s allowed_sockets [%s] worker_socket %d '
                          'worker_core %d' % (violation.name,
                                              valid,
                                              violation.assigned_node,
                                              violation.assigned_core))
                else:
                    print('name %s has no valid '
                          'placements worker_socket %d '
                          'worker_core %d' % (violation.name,
                                              violation.assigned_node,
                                              violation.assigned_core))
            for module in response.modules:
                print('constraints violated for module %s --'
                      ' please check bessd log' % module.name)
            error = True
        if response.fatal:
            raise self.ConstraintError("Fatal violation of "
                                       "scheduling constraints")
        return error

    def uncheck_resume_all(self):
        return self._request('ResumeAll')

    def check_resume_all(self):
        ret = self.check_constraints()
        self.uncheck_resume_all()
        return ret

    def resume_all(self, check=True):
        if check:
            return self.check_resume_all()
        else:
            return self.uncheck_resume_all()

    def resume_worker(self, wid):
        request = bess_msg.ResumeWorkerRequest()
        request.wid = wid
        return self._request('ResumeWorker', request)

    def check_scheduling_constraints(self):
        return self._request('CheckSchedulingConstraints')

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

        message_type = getattr(port_msg, driver + 'Arg', module_msg.EmptyArg)
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

    def import_plugin(self, path):
        request = bess_msg.ImportPluginRequest()
        request.path = path
        return self._request('ImportPlugin', request)

    def unload_plugin(self, path):
        request = bess_msg.UnloadPluginRequest()
        request.path = path
        return self._request('UnloadPlugin', request)

    def list_plugins(self):
        return self._request('ListPlugins')

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

        message_type = getattr(module_pb, mclass + 'Arg', module_msg.EmptyArg)
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
        request = bess_msg.CommandRequest()
        request.name = name
        request.cmd = cmd

        try:
            message_type = getattr(module_pb, arg_type)
        except AttributeError as e:
            raise self.APIError('Unknown arg "%s"' % arg_type)

        try:
            arg_msg = pb_conv.dict_to_protobuf(message_type, arg)
        except (KeyError, ValueError) as e:
            raise self.APIError(e)

        request.arg.Pack(arg_msg)

        try:
            response = self._request('ModuleCommand', request)
        except self.Error as e:
            e.info.update(module=name, command=cmd, command_arg=arg)
            raise

        if response.HasField('data'):
            response_type_str = response.data.type_url.split('.')[-1]
            response_type = getattr(module_pb, response_type_str,
                                    module_msg.EmptyArg)
            result = response_type()
            response.data.Unpack(result)
            return result
        else:
            return response

    def _configure_gate_hook(self, hook, module,
                             arg, enable=None, direction=None, gate=None):
        if gate is None:
            gate = -1
        if direction is None:
            direction = 'out'
        if enable is None:
            enable = False
        request = bess_msg.ConfigureGateHookRequest()
        request.hook_name = hook
        request.module_name = module
        request.enable = enable
        if direction == 'in':
            request.igate = gate
        elif direction == 'out':
            request.ogate = gate
        else:
            raise self.APIError('direction must be either "out" or "in"')
        request.arg.Pack(arg)
        return self._request('ConfigureGateHook', request)

    def tcpdump(self, enable, m, direction='out', gate=0, fifo=None):
        arg = bess_msg.TcpdumpArg()
        if fifo is not None:
            arg.fifo = fifo
        return self._configure_gate_hook('tcpdump', m, arg, enable, direction,
                                         gate)

    def track_module(self, m, enable, bits=False, direction='out', gate=-1):
        arg = bess_msg.TrackArg()
        arg.bits = bits
        return self._configure_gate_hook('track', m, arg, enable, direction,
                                         gate)

    def list_workers(self):
        return self._request('ListWorkers')

    def add_worker(self, wid, core, scheduler=None):
        request = bess_msg.AddWorkerRequest()
        request.wid = wid
        request.core = core
        request.scheduler = scheduler or ''
        return self._request('AddWorker', request)

    def destroy_worker(self, wid):
        request = bess_msg.DestroyWorkerRequest()
        request.wid = wid
        return self._request('DestroyWorker', request)

    def list_tcs(self, wid=-1):
        request = bess_msg.ListTcsRequest()
        request.wid = wid

        return self._request('ListTcs', request)

    def add_tc(self, name, policy, wid=-1, parent='', resource=None,
               priority=None, share=None, limit=None, max_burst=None,
               leaf_module_name=None, leaf_module_taskid=None):
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
        if leaf_module_name is not None:
            class_.leaf_module_name = leaf_module_name
        if leaf_module_taskid is not None:
            class_.leaf_module_taskid = leaf_module_taskid

        return self._request('AddTc', request)

    def update_tc_params(self, name, resource=None, limit=None, max_burst=None,
                         leaf_module_name=None, leaf_module_taskid=0):
        request = bess_msg.UpdateTcParamsRequest()
        class_ = getattr(request, 'class')
        class_.name = name
        if resource is not None:
            class_.resource = resource

        if limit:
            for k in limit:
                class_.limit[k] = limit[k]

        if max_burst:
            for k in max_burst:
                class_.max_burst[k] = max_burst[k]

        if leaf_module_name is not None:
            class_.leaf_module_name = leaf_module_name
        if leaf_module_taskid is not None:
            class_.leaf_module_taskid = leaf_module_taskid

        return self._request('UpdateTcParams', request)

    # Attach the task numbered `module_taskid` (usually modules only have one
    # task, numbered 0) from the module `module_name` to a TC tree.
    #
    # The behavior differs based on the arguments provided:
    #
    # * If `wid` is specified, the task is attached as a root in the worker
    #   `wid`.  If `wid` has multiple roots they will be under a default
    #   round-robin policy.
    # * If `parent` is specified, the task is attached as a child of `parent`.
    #   If `parent` is a priority or weighted_fair TC, `priority` or `share`
    #   can be used to customize the child parameter.
    #
    def attach_task(self, module_name, parent='', wid=-1,
                    module_taskid=0, priority=None, share=None):
        request = bess_msg.UpdateTcParentRequest()
        class_ = getattr(request, 'class')
        class_.leaf_module_name = module_name
        class_.leaf_module_taskid = module_taskid
        class_.parent = parent
        class_.wid = wid

        if priority is not None:
            class_.priority = priority

        if share is not None:
            class_.share = share

        return self._request('UpdateTcParent', request)

    # Deprecated alias for attach_task
    def attach_module(self, *args, **kwargs):
        return self.attach_task(*args, **kwargs)

    def get_tc_stats(self, name):
        request = bess_msg.GetTcStatsRequest()
        request.name = name
        return self._request('GetTcStats', request)

    def dump_mempool(self, socket=-1):
        request = bess_msg.DumpMempoolRequest()
        request.socket = socket
        return self._request('DumpMempool', request)
