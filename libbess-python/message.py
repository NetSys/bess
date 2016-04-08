import struct

TYPE_NIL    = 0
TYPE_INT    = 1
TYPE_DOUBLE = 2
TYPE_STR    = 3
TYPE_BLOB   = 4
TYPE_LIST   = 5
TYPE_MAP    = 6

# a custom class that supports both obj[x] and obj.x for convenience
class SNObjDict(object):
    def __init__(self):
        # self._dict = dict() causes a recursive call
        self.__dict__['_dict'] = dict()

    def __str__(self):
        return self._dict.__str__()

    def __repr__(self):
        return self._dict.__repr__()

    def __getattr__(self, name):
        if name.startswith('__'):
            return self._dict.__getattribute__(name)
        else: 
            return self._dict[name]

    def __setattr__(self, name, value):
        self._dict[name] = value

    def __delattr__(self, name):
        del self._dict[name]

    def __getitem__(self, key):
        return self._dict[key]

    def __setitem__(self, key, value):
        self._dict[key] = value

    def __delitem__(self, key):
        del self._dict[key]

    def __contains__(self, key):
        return key in self._dict

    def __iter__(self):
        return self._dict.__iter__()

    @staticmethod
    def __do_op(x, y, op, rop):
        ret = x.__getattribute__(op)(y)
        if ret == NotImplemented:
            ret = y.__getattribute__(rop)(x)
        return ret

    def __op(a, b, op, rop):
        a_keys = set(a._keys())
        if isinstance(b, dict):
            b_keys = set(b.keys())
        elif isinstance(b, SNObjDict):
            b_keys = set(b._keys())
        elif isinstance(b, (int, float)):
            ret = SNObjDict()
            for k in a_keys:
                ret[k] = a.__do_op(a[k], b, op, rop)
            return ret
        else:
            return NotImplemented

        if a_keys != b_keys:
            raise TypeError('Attritubes %s are not in common', \
                    set.symmetric_difference(a_keys, b_keys))

        ret = SNObjDict()
        for k in a_keys:
            ret[k] = a.__do_op(a[k], b[k], op, rop)
        return ret

    def __add__(a, b):
        return a.__op(b, '__add__', '__radd__')

    def __sub__(a, b):
        return a.__op(b, '__sub__', '__rsub__')

    def __mul__(a, b):
        return a.__op(b, '__mul__', '__rmul__')

    def __div__(a, b):
        return a.__op(b, '__div__', '__rdiv__')

    # intended to be used by controller
    def _keys(self):
        return self._dict.keys()

def encode(obj):
    def zero_pad8(buf, num_bytes):
        while num_bytes % 8:
            num_bytes += 1

        return struct.pack(str(num_bytes) + 's', buf)
        
    def encode_cstr(cstr):
        return zero_pad8(cstr, len(cstr) + 1)

    if obj == None:
        t = TYPE_NIL
        l = 0
        v = ''
    elif isinstance(obj, int):
        t = TYPE_INT
        l = 8
        v = struct.pack('<q', obj)
    elif isinstance(obj, float):
        t = TYPE_DOUBLE
        l = 8
        v = struct.pack('<d', obj)
    elif isinstance(obj, str):
        t = TYPE_STR
        l = len(obj) + 1
        v = encode_cstr(obj)
    elif isinstance(obj, bytearray):
        t = TYPE_BLOB
        l = len(obj)
        v = zero_pad8(str(obj), len(obj))
    elif isinstance(obj, (list, set)):
        t = TYPE_LIST
        l = len(obj)
        v = ''.join(map(encode, obj))
    elif isinstance(obj, (dict, SNObjDict)):
        t = TYPE_MAP
        if isinstance(obj, SNObjDict):
            obj = obj._dict
        keys = sorted(map(str, obj.keys())) # all keys must be a string
        l = len(keys)
        v = ''.join(map(lambda k: encode_cstr(k) + encode(obj[k]), keys))
    else:
        raise Exception('Unsupported type %s' % type(obj))

    return struct.pack('<LL', t, l) + v

# returns (obj, new offset) tuple
def _decode_recur(buf, offset):

    t, l = struct.unpack_from('<LL', buf, offset)
    offset += 8

    if t == TYPE_NIL:
        v = None
    elif t == TYPE_INT:
        v, = struct.unpack_from('<q', buf, offset)
        offset += 8
    elif t == TYPE_DOUBLE:
        v, = struct.unpack_from('<d', buf, offset)
        offset += 8
    elif t == TYPE_STR:
        v = str(buf[offset:offset + l - 1])
        offset += l
    elif t == TYPE_BLOB:
        v = bytearray(buf[offset:offset + l])
        offset += l
    elif t == TYPE_LIST:
        v  = list()
        for i in xrange(l):
            obj, offset = _decode_recur(buf, offset)
            v.append(obj)
    elif t == TYPE_MAP:
        v = SNObjDict()
        for i in xrange(l):
            z_pos = buf.find('\0', offset)
            if z_pos == -1:
                raise Exception('non-null terminating key')
            key = buf[offset:z_pos]

            offset = z_pos + 1
            while offset % 8:
                offset += 1

            obj, offset = _decode_recur(buf, offset)
            v[key] = obj

    else:
        raise Exception('Unsupported type id %d' % t)

    while offset % 8:
        offset += 1

    return v, offset

def decode(buf):
    try:
        obj, consumed = _decode_recur(buf, 0)
        if consumed != len(buf):
            raise Exception('%dB buffer, but only %dB consumed' % 
                    (len(buf), consumed))
        return obj
    except Exception as e:
        print >> sys.stderr, 'Decoding error. Len=%-5d' % len(buf),
        for c in buf:
            print >> sys.stderr, '%02x' % ord(c), 
        print >> sys.stderr
        raise e
