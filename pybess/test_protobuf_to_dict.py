from __future__ import absolute_import
import unittest
from . import bess_msg_pb2 as bess_msg
from . import bess_test_msg_pb2 as test_msg
from . import protobuf_to_dict as pb_conv


class TestProtobufConvert(unittest.TestCase):

    def test_protobuf_to_dict(self):
        pb = bess_msg.CreatePortResponse()
        pb.error.code = 1
        pb.error.errmsg = 'bar'
        pb.name = 'foo'

        true_result = {
            'error': {
                'code': 1,
                'errmsg': 'bar',
            },
            'name': 'foo'
        }
        result = pb_conv.protobuf_to_dict(pb)
        self.assertEqual(true_result, result)

    def test_dict_to_protobuf(self):
        pb = bess_msg.CreatePortResponse()
        pb.error.code = 1
        pb.error.errmsg = 'bar'
        pb.name = 'foo'

        msg_dict = {
            'error': {
                'code': 1,
                'errmsg': 'bar',
            },
            'name': 'foo'
        }

        msg = pb_conv.dict_to_protobuf(bess_msg.CreatePortResponse, msg_dict)
        self.assertEqual(msg, pb)

        pb = bess_msg.CreateModuleRequest()
        pb.name = 'm1'
        pb.mclass = 'bpf'

        kv = {
            'name': 'm1',
            'mclass': 'bpf',
        }
        msg = pb_conv.dict_to_protobuf(bess_msg.CreateModuleRequest, kv)
        self.assertEqual(msg, pb)

    def test_dict_to_protobuf_msg_has_nested_dict(self):
        pb = test_msg.NestedDictMsg()

        # pb.a = test_msg.UnnestedDictMsg()
        a_dict = {7: 1, 8: 10}
        init_from_dict(pb.a.dict, a_dict)

        b_dict = {7: 5}
        init_from_dict(pb.b.dict, b_dict)

        c_dict = {110: 10, 7: 5, 6: 7}
        init_from_dict(pb.c.dict, c_dict)

        msg_dict = {
            'a': {'dict': a_dict},
            'b': {'dict': b_dict},
            'c': {'dict': c_dict},
        }

        msg = pb_conv.dict_to_protobuf(test_msg.NestedDictMsg, msg_dict)
        self.assertEqual(msg, pb)

    def test_dict_to_protobuf_msg_is_unnested_dict(self):
        pb = test_msg.UnnestedDictMsg()
        init_from_dict(pb.dict, {0: 10, 1: 5, 6: 7})

        msg_dict = {
            'dict': {0: 10, 1: 5, 6: 7}
        }

        msg = pb_conv.dict_to_protobuf(test_msg.UnnestedDictMsg, msg_dict)
        self.assertEqual(msg, pb)

def init_from_dict(pb_map, d):
    for k, v in d.items():
        pb_map[k] = v
