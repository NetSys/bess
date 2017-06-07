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
            ('a::SomeModule()',         "__bess_module__('a', 'SomeModule', )"),
            ('a::SomeModule(b, c, d)',  "__bess_module__('a', 'SomeModule', b, c, d)"),
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


def test_generator(path):
    def test(self):
        xformed = sugar.xform_file(path)
        code = compile(xformed, path, 'exec')

    return test


for root, dir_names, file_names in os.walk(script_dir):
    for file_name in fnmatch.filter(file_names, "*.bess"):
        path = os.path.join(root, file_name)
        test_name = 'test' + path.replace('/', '_').replace('.', '_')
        test_method = test_generator(path)
        setattr(TestSugar, test_name, test_method)

if __name__ == '__main__':
    unittest.main()
