import socket
import struct
import errno
import sys
import os

import message

class SoftNIC(object):

    # errors from SoftNIC daemon
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
        return self.s is not None

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
            raise self.APIError('Cannot connect to %s:%d (BESS not running?)' \
                    % (host, port))

    def disconnect(self):
        if self.is_connected():
            self.s.close()
            self.s = None
        else:
            raise self.APIError('Not connected to BESS daemon')

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

        self.s.sendall(struct.pack('<L', len(q)))
        self.s.sendall(q)

        total, = struct.unpack('<L', self.s.recv(4))
        buf = []
        received = 0
        while received < total:
            frag = self.s.recv(total - received)
            buf.append(frag)
            received += len(frag)

        try:
            obj = message.decode(''.join(buf))
        except:
            print >> sys.stderr, 'Decoding error, binary: %s' % obj.encode('hex')
            raise

        if self.debug:
            print >> sys.stderr, '\t<--- %s' % repr(obj)

        if isinstance(obj, dict) and 'err' in obj:
            err = obj['err']
            errmsg = obj.get('errmsg', '(error message is not given)')
            details = obj.get('details', None)
            raise self.Error(err, errmsg, details)

        return obj

    def _request_softnic(self, cmd, arg = None):
        if arg is not None:
            return self._request({'to': 'softnic', 'cmd': cmd, 'arg': arg})
        else:
            return self._request({'to': 'softnic', 'cmd': cmd})

    def _request_module(self, name, cmd, arg = None):
        if arg is not None:
            return self._request({'to': 'module', 'name': name, 'cmd': cmd, 
                    'arg': arg}) 
        else:
            return self._request({'to': 'module', 'name': name, 'cmd': cmd})

    def kill(self):
        return self._request_softnic('kill_bess')

    def reset_all(self):
        return self._request_softnic('reset_all')


    def pause_all(self):
        return self._request_softnic('pause_all')

    def resume_all(self):
        return self._request_softnic('resume_all')

    def list_drivers(self):
        return self._request_softnic('list_drivers')

    def reset_ports(self):
        return self._request_softnic('reset_ports')

    def list_ports(self):
        return self._request_softnic('list_ports')

    def create_port(self, driver = 'PMD', name = None, arg = None):
        kv = {'driver': driver}

        if name != None:    kv['name'] = name
        if arg != None:     kv['arg'] = arg

        return self._request_softnic('create_port', kv)

    def destroy_port(self, name):
        return self._request_softnic('destroy_port', name)

    def get_port_stats(self, port):
        return self._request_softnic('get_port_stats', port)

    def list_modules(self):
        return self._request_softnic('list_modules')

    def list_mclasses(self):
        return self._request_softnic('list_mclasses')

    def reset_modules(self):
        return self._request_softnic('reset_modules')

    def create_module(self, mclass, name = None, arg = None):
        kv = {'mclass': mclass}

        if name != None:    kv['name'] = name
        if arg != None:     kv['arg'] = arg

        return self._request_softnic('create_module', kv)

    def destroy_module(self, name):
        return self._request_softnic('destroy_module', name)

    def get_module_info(self, name):
        return self._request_softnic('get_module_info', name)

    def connect_modules(self, m1, m2, gate = 0):
        return self._request_softnic('connect_modules', 
                {'m1': m1, 'm2': m2, 'gate': gate})

    def disconnect_modules(self, name, gate = 0):
        return self._request_softnic('disconnect_modules', 
                {'name': name, 'gate': gate})

    def query_module(self, name, arg):
        return self._request_module(name, 'query', arg)

    def enable_tcpdump(self, fifo, m, gate = 0):
        args = {'name': m, 'gate': gate, 'fifo': fifo}
        return self._request_softnic('enable_tcpdump', args)

    def disable_tcpdump(self, m, gate = 0):
        args = {'name': m, 'gate': gate}
        return self._request_softnic('disable_tcpdump', args)
