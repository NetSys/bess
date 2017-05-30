import sugar
import unittest


class TestSugar(unittest.TestCase):
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


if __name__ == '__main__':
    unittest.main()
