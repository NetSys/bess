import struct

TYPE_NIL    = 0
TYPE_INT    = 1
TYPE_DOUBLE = 2
TYPE_STR    = 3
TYPE_BLOB   = 4
TYPE_LIST   = 5
TYPE_MAP    = 6

def hexdump(buf):
    print 'Len=%-5d' % len(buf),
    for c in buf:
        print '%02x' % ord(c), 
    print

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
    elif isinstance(obj, dict):
        t = TYPE_MAP
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
        v = dict()
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
        hexdump(buf)
        raise e
