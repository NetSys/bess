import re
import unittest
import traceback

'''
<Ringo language>
- Providing a click-like module connection semantics
- All these syntactic sugars must be able to coexist with original Python syntax.
e.g.,
    print 'hello %s' % (%ENV!'Anynomous')

--------------------------------------------------------------------------------
Syntax               Semantic                      Plain Python code
--------------------------------------------------------------------------------
$ENV                 The value of environment      __bess_env__('ENV')
                     variable ENV (string)
                     No empty string exists

$ENV!str             If ENV not exists, use x      __bess_env__('ENV', str)
                     (x can be a string literal,
                     variable of a string literal,
                     or parenthesis string literal
                     expression)

a::SomeModule()      Create a SomeModule and       __bess_module__('a',
                     assign into variable a          'SomeModule')

a::SomeModule(...)   Additional parameters for     __bess_module__('a',
                     creating module                 'SomeModule', ...)

a,b::SomeModule()    Create two SomeModule and     __bess_module__(('a','b'),
                     assign into a and b             'SomeModule')

a->b                 Connect a and b               a + b

a:x->b               Connect output gate x of a    a*i + b
                     and b                         a.connect(next_mod=b,
                     (x should be an integer)        ogate=x)

a->y:b               Connect a to input gate y     a + y*b
                     of b                          a.connect(next_mod=b,
                     (x should be an integer)        igate=y)

a:3->4:b             Connect output gate 3 of a    a*3 + 4*b
                     and input gate of 4           a.connect(next_mod=b,
                                                     ogate=3, igate=4)

--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
Usage examples
--------------------------------------------------------------------------------

1. Chaining multiple module connection
Ringo:
    a -> b:1 -> c
Python:
    a + b*1 + c

2. Create and connect at once
Ringo:
    a::Foo() -> b::Bar()
Python:
    __bess_module__('a','Foo') + __bess_module__('b', 'Bar')

3. Create anonymous modules and connection them
Ringo:
    Foo() -> Bar()
Python:
    Foo() + Bar()

4. Composition example
Ringo:
    a::Foo():3 -> b
Python:
    __bess_module__('a', 'Foo')*3 + b

5. 
a::Foo():1 -> 2:b::Foo()
- Connecting output gate 1 of a to input gate 2 of b
'''

# common python tokens
NAME = r'[a-zA-Z_]\w*'
COMMENT = r'#[^\r\n]*'
STRING_SHORT = r'\'.*?\'|\".*?\"'
STRING_LONG = r'\'\'\'.*?\'\'\'|""".*?"""'
STRING_ALL = STRING_LONG + '|' + STRING_SHORT

def replace_envvar(s):
    environment = r'\$(' + NAME + ')'\
            r'(!(' + STRING_SHORT + '|' + NAME + '))?' \
            r'(!\()?'
    
    # first group: # leading COMMENT -> skip
    # second group: single / double /triple quoted strings -> skip
    # third group: dollar sign with NAME -> $var!assignment
    # third group consists of
    #   fourth group: environment variable
    #   fifth group: 
    #   e.g., $ENV!'100'
    #       third group  "$ENV!'100'"
    #       fourth group 'ENV'
    #       fifth group '!' or not
    
    pattern = '(' + COMMENT + ')|('+ STRING_ALL + ')|(' + environment + ')'
    regex = re.compile(pattern, re.MULTILINE|re.DOTALL)
   
    def _replacer(match):
        if match.group(3) is not None:
            # from our definition of pattern 
            # if match.group(3) is not None, match.group(4) is not None
            # if match.group(5) is not None, then there is a parameter
            if match.group(5) is  None and match.group(7) is None:
                return "__bess_env__('" + match.group(4) + "')"
            elif match.group(5) is not None:
                return "__bess_env__('" + match.group(4) + "', " + \
                        match.group(6) + ")" 
            else: 
                return "__bess_env__('" + match.group(4) + "', "
            
        else:
            return match.group()

    s = regex.sub(_replacer, s)
    return s

def replace_rarrows(s):
    target = r'(:([^:\s]+)[\s]*)?->([\s]*([^:\s]+):)?'
    # first group: # leading COMMENT -> skip
    # second group: single / double /triple quoted strings -> skip
    # third group: replace target '->' 
    pattern = '(' + COMMENT + ')|('+ STRING_ALL + ')|(' + target + ')'
    regex = re.compile(pattern, re.MULTILINE|re.DOTALL)
    
    def _replacer(match):
        def parenthesize(exp):
            if exp.isalnum():
                return exp
            else:
                return '(%s)' % exp

        if match.group(3):
            prefix = ''
            postfix = ''

            if match.group(4):
                prefix = '*%s ' % parenthesize(match.group(5))
            if match.group(6):
                postfix = ' %s*' % parenthesize(match.group(7))
            return prefix + '+' + postfix
        else:
            return match.group()
        
    return regex.sub(_replacer, s)

def create_module_string(s):

    # single module -> return a module name string
    if s.find(',') < 0:
        return "'" + s + "'" 

    # multiple module -> return a tuple of module name string
    mstr = '('
    for module in s.split(','):
       mstr += "'"+module.strip()+"', "
    mstr += ')'
    return mstr

def replace_module_assignment(s):
    target = '(' + NAME + '(, *' + NAME + ')*' + ')::(' + NAME + ')\('
            
    # first group: # leading COMMENT -> skip
    # second group: single / double /triple quoted strings -> skip
    # third group: replace target  'NAME::NAME'
    pattern = '(' + COMMENT + ')|('+ STRING_ALL + ')|(' + target + ')'
    regex = re.compile(pattern, re.MULTILINE|re.DOTALL)
    
    def _replacer(match):
        if match.group(3) is not None:
            # if match.group(3) is not None, 
            # match.group(4), match.group(6) is not None
            # match.group(4) -> module NAMEs
            # match.group(6) -> module class NAME
            modules = create_module_string(match.group(4))
            f_str = "__bess_module__("+ modules + ", '" + \
                    match.group(6) + "', "
            return f_str

        else:
            return match.group()

    return regex.sub(_replacer,s)

def xform_str(s):
    s = replace_envvar(s)
    s = replace_module_assignment(s)
    s = replace_rarrows(s)
    return s

def xform_file(filename):
    with open(filename) as f:
        return xform_str(f.read())

def _test(suite):
    failed = 0
    for i, case in enumerate(suite):
        str_input, str_expected = case
        str_output = xform_str(str_input)

        print 'Testcase %2d: %-30s %s' % (i + 1, str_input, str_output)
        if str_output != str_expected:
            print '%s !! Expected: %s' % (' ' * 30, str_expected)
            failed += 1

    if failed == 0:
        print 'OK'
    else:
        print '%d test cases failed' % failed

def _run_tests():
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

    mod_suite = [
        ('a::SomeModule()',         "__bess_module__('a', 'SomeModule', )"),
        ('a::SomeModule(b, c, d)',  "__bess_module__('a', 'SomeModule', b, c, d)"),
        ('a > b',                   "a > b"), 
        ('a -> b',                  "a + b"), 
        ('ab->cd',                  "ab+cd"), 
        ('abc:2 -> def',            "abc*2 + def"),
        ('abc -> 3:def',            "abc + 3*def"),
        ('a1 -> b1 -> c1',          "a1 + b1 + c1"), 
        ('xx ->yy :0-> zz',         "xx +yy *0 + zz"),
        ('aa:0 -> 1:bb:23 -> 4:cc', "aa*0 + 1*bb*23 + 4*cc"),
        ('a:i+1 -> b',              "a*(i+1) + b"),
        ('a -> hello:b',            "a + hello*b"),
        ('a -> b -> c',             "a + b + c"), 
        ('a::Foo() -> b::Bar()', 
            "__bess_module__('a', 'Foo', ) + __bess_module__('b', 'Bar', )"),
        ('a::Foo(b, c, d) -> b::Bar()', 
            "__bess_module__('a', 'Foo', b, c, d) + __bess_module__('b', 'Bar', )"),
        ('a::Foo():xxx -> b::Bar()', 
            "__bess_module__('a', 'Foo', )*xxx + __bess_module__('b', 'Bar', )"),
        ('a::Foo(b, c, d):3->2:b::Bar()', 
            "__bess_module__('a', 'Foo', b, c, d)*3 + 2*__bess_module__('b', 'Bar', )"),
        ('Foo() -> Bar()',         'Foo() + Bar()'),
        ('Foo():0 -> Bar()',       'Foo()*0 + Bar()'),
        ('Foo() -> 1:Bar()',       'Foo() + 1*Bar()'),
    ]

    _test(env_suite)
    _test(mod_suite)

if __name__ == '__main__':
    _run_tests();
