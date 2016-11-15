import socket
import struct
import errno
import sys
import os

import message

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
        self.s = None
        self.peer = None

    def is_connected(self):
        if self.s is None:
            return False

        try:
            tmp = self.s.recv(1, socket.MSG_DONTWAIT)
            assert len(tmp) == 0, 'Bogus data from BESS daemon'
        except socket.error as e:
            if e.errno not in [errno.EAGAIN, errno.EWOULDBLOCK]:
                self.s.close()
                self.s = None
                raise e

        return True

    def connect(self, host='localhost', port=DEF_PORT):
        if self.is_connected():
            raise self.APIError('Already connected')

        try:
            self.s = socket.socket()
            self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.s.connect((host, port))
            self.peer = (host, port)
        except socket.error:
            self.s = None
            self.peer = None
            raise self.APIError('Cannot connect to %s:%d ' \
                    '(BESS daemon not running?)' \
                    % (host, port))

    def disconnect(self):
        try:
            self.s.close()
        except:
            pass
        finally:
            self.s = None

    def set_debug(self, flag):
        self.debug = flag

    # (de)serialization only happens in this private method
    def _request(self, obj):
        if not self.is_connected():
            raise self.APIError('Not connected to BESS daemon')

        if self.debug:
            print >> sys.stderr, '\t---> %s' % repr(obj)

        try:
            q = message.encode(obj)
        except:
            print >> sys.stderr, 'Encoding error, object: %s' % repr(obj)
            raise

        self.s.sendall(struct.pack('<L', len(q)) + q)

        total, = struct.unpack('<L', self.s.recv(4))
        buf = []
        received = 0
        while received < total:
            frag = self.s.recv(total - received)
            buf.append(frag)
            received += len(frag)

        obj = message.decode(''.join(buf))

        if self.debug:
            print >> sys.stderr, '\t<--- %s' % repr(obj)

        if isinstance(obj, message.SNObjDict) and 'err' in obj:
            err = obj['err']
            errmsg = obj['errmsg'] if 'errmsg' in obj else \
                    '(error message is not given)'
            details = obj['details'] if 'details' in obj else None
            raise self.Error(err, errmsg, details)

        return obj

    def _request_bess(self, cmd, arg=None):
        if arg is not None:
            return self._request({'to': 'bess', 'cmd': cmd, 'arg': arg})
        else:
            return self._request({'to': 'bess', 'cmd': cmd})

    def _request_module(self, name, cmd, arg=None):
        if arg is not None:
            return self._request({'to': 'module', 'name': name, 'cmd': cmd,
                    'arg': arg})
        else:
            return self._request({'to': 'module', 'name': name, 'cmd': cmd})

    def kill(self):
        try:
            return self._request_bess('kill_bess')
        except socket.error:
            self.disconnect()

    def reset_all(self):
        return self._request_bess('reset_all')

    def pause_all(self):
        return self._request_bess('pause_all')

    def resume_all(self):
        return self._request_bess('resume_all')

    def list_drivers(self):
        return self._request_bess('list_drivers')

    def get_driver_info(self, name):
        return self._request_bess('get_driver_info', name)

    def reset_ports(self):
        return self._request_bess('reset_ports')

    def list_ports(self):
        return self._request_bess('list_ports')

    def create_port(self, driver, name=None, arg=None):
        kv = {'driver': driver}

        if name is not None:    kv['name'] = name
        if arg is not None:     kv['arg'] = arg

        return self._request_bess('create_port', kv)

    def destroy_port(self, name):
        return self._request_bess('destroy_port', name)

    def get_port_stats(self, port):
        return self._request_bess('get_port_stats', port)

    def list_mclasses(self):
        return self._request_bess('list_mclasses')

    def list_modules(self):
        return self._request_bess('list_modules')

    def get_mclass_info(self, name):
        return self._request_bess('get_mclass_info', name)

    def reset_modules(self):
        return self._request_bess('reset_modules')

    def create_module(self, mclass, name=None, arg=None):
        kv = {'mclass': mclass}

        if name is not None:    kv['name'] = name
        if arg is not None:     kv['arg'] = arg

        return self._request_bess('create_module', kv)

    def destroy_module(self, name):
        return self._request_bess('destroy_module', name)

    def get_module_info(self, name):
        return self._request_bess('get_module_info', name)

    def connect_modules(self, m1, m2, ogate=0, igate=0):
        return self._request_bess('connect_modules',
                {'m1': m1, 'm2': m2, 'ogate': ogate, 'igate': igate})

    def disconnect_modules(self, name, ogate = 0):
        return self._request_bess('disconnect_modules',
                {'name': name, 'ogate': ogate})

    def run_module_command(self, name, cmd, arg):
        return self._request_module(name, cmd, arg)

    def enable_tcpdump(self, fifo, m, direction='out', gate=0):
        args = {'name': m, 'is_igate': int(direction == 'in'), 'gate': gate,
                'fifo': fifo}
        return self._request_bess('enable_tcpdump', args)

    def disable_tcpdump(self, m, direction='out', gate=0):
        args = {'name': m, 'is_igate': int(direction == 'in'), 'gate': gate}
        return self._request_bess('disable_tcpdump', args)

    def enable_track(self, m, direction='out', gate=None):
        args = {'name': m, 'gate_idx': gate, 'is_igate': int(direction == 'in')}
        return self._request_bess('enable_track', args)

    def disable_track(self, m, direction='out', gate=None):
        args = {'name': m, 'gate_idx': gate, 'is_igate': int(direction == 'in')}
        return self._request_bess('disable_track', args)

    def list_workers(self):
        return self._request_bess('list_workers')

    def add_worker(self, wid, core):
        args = {'wid': wid, 'core': core}
        return self._request_bess('add_worker', args)

    def attach_task(self, m, tid=0, tc=None, wid=None):
        if (tc is None) == (wid is None):
            raise self.APIError('You should specify either "tc" or "wid"' \
                    ', but not both')

        if tc is not None:
            args = {'name': m, 'taskid': tid, 'tc': tc}
        else:
            args = {'name': m, 'taskid': tid, 'wid': wid}

        return self._request_bess('attach_task', args)

    def list_tcs(self, wid = None):
        args = None
        if wid is not None:
            args = {'wid': wid}

        return self._request_bess('list_tcs', args)

    def add_tc(self, name, wid=0, priority=0, limit=None, max_burst=None):
        args = {'name': name, 'wid': wid, 'priority': priority}
        if limit:
            args['limit'] = limit

        if max_burst:
            args['max_burst'] = max_burst

        return self._request_bess('add_tc', args)

    def get_tc_stats(self, name):
        return self._request_bess('get_tc_stats', name)
