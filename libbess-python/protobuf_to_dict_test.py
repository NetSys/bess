import unittest
import bess_msg_pb2 as bess_msg
import protobuf_to_dict as pb_conv


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
