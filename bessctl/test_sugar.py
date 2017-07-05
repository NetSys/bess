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

from __future__ import print_function
from __future__ import absolute_import
from __future__ import unicode_literals
import fnmatch
import os
import unittest

from . import sugar

this_dir = os.path.dirname(os.path.realpath(__file__))
script_dir = os.path.join(this_dir, 'conf')


class TestSugar(unittest.TestCase):

    """
    All scripts in conf/ will be dynamically added here as individual
    tests (e.g., test_path_to_conf_samples_exactmatch_bess) in this class.
    The tests will just perform nothing but sugar.xform_file() and
    check if the resulting code is syntactically correct.
    This is to see if it causes any exceptions during the process.
    """

    def run_suite(self, suite):
        for case in suite:
            str_input, str_expected = case
            str_output = sugar.xform_str(str_input)

            self.assertEqual(str_output, str_expected)

    def test_envvar(self):
        env_suite = [
            ('$ENV',                "__bess_env__('ENV')"),
            ("$ENV!x",              "__bess_env__('ENV', x)"),
            ("$ENV!'100'",          "__bess_env__('ENV', '100')"),
            ("$ENV!'DPDK'",         "__bess_env__('ENV', 'DPDK')"),
            ("$ENV!('DPDK')",       "__bess_env__('ENV', 'DPDK')"),
            ("if (3 != 4)",         "if (3 != 4)"),
            ("' $ENV '",            "' $ENV '"),
            ('" $ENV "',            '" $ENV "'),
            ('# $ENV ',             '# $ENV '),
        ]

        self.run_suite(env_suite)

    def test_module(self):
        mod_suite = [
            ('a::SomeModule()',
             "__bess_module__('a', 'SomeModule', )"),
            ('a::SomeModule(b, c, d)',
             "__bess_module__('a', 'SomeModule', b, c, d)"),
            ('a > b',                   "a > b"),
            ('a >- b',                  "a >- b"),
            ('a -> b',                  "a + b"),
            ('ab->cd',                  "ab+cd"),
            ('abc:2 -> def',            "abc*2 + def"),
            ('abc -> 3:def',            "abc + 3*def"),
            ('a1 -> b1 -> c1',          "a1 + b1 + c1"),
            ('xx ->yy :0-> zz',         "xx +yy *0+ zz"),
            ('aa:0 -> 1:bb:23 -> 4:cc', "aa*0 + 1*bb*23 + 4*cc"),
            ('a:i+1 -> b',              "a*(i+1) + b"),
            ('a -> j+1:b',              "a + (j+1)*b"),
            ('a -> hello:b',            "a + hello*b"),
            ('a -> b -> c',             "a + b + c"),
            ('a::Foo() -> b::Bar()',
                "__bess_module__('a', 'Foo', ) + __bess_module__('b', 'Bar', )"),
            ('a::Foo(b, c, d) -> b::Bar()',
                "__bess_module__('a', 'Foo', b, c, d) + __bess_module__('b', 'Bar', )"),
            ('a::Foo():xxx -> b::Bar()',
                "__bess_module__('a', 'Foo', )*xxx + __bess_module__('b', 'Bar', )"),
            ('a::Foo(b, c, d):3->2:b::Bar()',
                "__bess_module__('a', 'Foo', b, c, d)*3+2*__bess_module__('b', 'Bar', )"),
            ('Foo() -> Bar()',         'Foo() + Bar()'),
            ('Foo():0 -> Bar()',       'Foo()*0 + Bar()'),
            ('Foo() -> 1:Bar()',       'Foo() + 1*Bar()'),
            ('a:{1:2}[1] -> b', 'a*({1:2}[1]) + b'),
            ('a  -> {1:2}[1]:b', 'a  + ({1:2}[1])*b'),
            ('x -> c[5]:y', 'x + (c[5])*y'),
            ('x:b[2] -> y', 'x*(b[2]) + y'),
            ('x:b[2] -> c[5]:y', 'x*(b[2]) + (c[5])*y'),
            ('a:2\\\n -> y', 'a*2\\\n + y'),
            ('a -> \\\n2:y', 'a + \\\n2*y'),
            ('# a -> b', '# a -> b'),
            ('"a -> b"', '"a -> b"'),
            ('"""a -> b"""', '"""a -> b"""'),
            ('"""a \n-> b"""', '"""a \n-> b"""'),
            ("'a -> b'", "'a -> b'"),
            ("'''a -> b'''", "'''a -> b'''"),
        ]

        self.run_suite(mod_suite)


def generate_test_method(path):
    def template(self):
        xformed = sugar.xform_file(path)
        code = compile(xformed, path, 'exec')

    return template


for root, dir_names, file_names in os.walk(script_dir):
    for file_name in fnmatch.filter(file_names, "*.bess"):
        path = os.path.join(root, file_name)
        name = 'test' + path.replace('/', '_').replace('.', '_')
        method = generate_test_method(path)
        setattr(TestSugar, name, method)

if __name__ == '__main__':
    unittest.main()
