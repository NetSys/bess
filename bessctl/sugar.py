import re

## common python tokens
name = r'[a-zA-Z_]\w*'
comment = r'#[^\r\n]*'
short_strings = r'\'.*?\'|\".*?\"'
long_strings = r'\'\'\'.*?\'\'\'|""".*?"""'
all_strings = long_strings + '|' + short_strings

def replace_envvar(s):
    environment = r'\$(' + name + ')'\
            r'(!(' + short_strings + '|' + name + '))?' \
            r'(!\()?'
    
    # first group: # leading comments -> skip
    # second group: single / double /triple quoted strings -> skip
    # third group: dollar sign with name -> $var!assignment
    # third group is consist with 
    #   fourth group: environment variable
    #   fifth group: 
    #   e.g., $ENV!'100'
    #       third group  "$ENV!'100'"
    #       fourth group 'ENV'
    #       fifth group '!' or not
    
    pattern = '(' + comment + ')|('+ all_strings + ')|(' + environment + ')'
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
    target = '->'
    # first group: # leading comments -> skip
    # second group: single / double /triple quoted strings -> skip
    # third group: replace target '->' 
    pattern = '(' + comment + ')|('+ all_strings + ')|(' + target + ')'
    regex = re.compile(pattern, re.MULTILINE|re.DOTALL)
    
    def _replacer(match):
        if match.group(3) is not None:
            return '+'
        else:
            return match.group()

    return regex.sub(_replacer,s)

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
    target = '(' + name + '(, *' + name + ')*' + ')::(' + name + ')\('
            
    # first group: # leading comments -> skip
    # second group: single / double /triple quoted strings -> skip
    # third group: replace target  'name::name'
    pattern = '(' + comment + ')|('+ all_strings + ')|(' + target + ')'
    regex = re.compile(pattern, re.MULTILINE|re.DOTALL)
    
    def _replacer(match):
        if match.group(3) is not None:
            # if match.group(3) is not None, 
            # match.group(4), match.group(6) is not None
            # match.group(4) -> module names
            # match.group(6) -> module class name
            modules = create_module_string(match.group(4))
            f_str = "__bess_module__("+ modules + ", '" + \
                    match.group(6) + "', "
            return f_str

        else:
            return match.group()

    return regex.sub(_replacer,s)

def replace_module_connect(s):
    s = replace_rarrows(s)
    s = replace_module_assignment(s)
    return s

def xform_str(s):
    s = replace_envvar(s)
    s = replace_module_connect(s)
    return s

def xform_file(filename):
    return xform_str(open(filename).read())
