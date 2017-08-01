# Copyright (c) 2014-2016, The Regents of the University of California.
# Copyright (c) 2016-2017, Nefeli Networks, Inc.
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

from __future__ import absolute_import
import os
import sys
import unittest

# See bess.py for why this is here.
bipath = os.path.abspath(os.path.join(__file__, '..', 'builtin_pb'))
if bipath not in sys.path:
    sys.path.insert(1, bipath)
del bipath

from . import protobuf_to_dict as pb_conv
from .builtin_pb import bess_msg_pb2 as bess_msg
from .builtin_pb import test_msg_pb2 as test_msg


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
