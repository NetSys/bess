from port import Port

class Module(object):
    def __init__(self, name = None, arg = None, **kwargs):
        self.name = '<uninitialized>'
 
        ret = self.softnic.create_module(self.__class__.__name__, name, 
                self.choose_arg(arg, kwargs))

        self.name = ret['name']
        #print 'Module %s created' % self

    def __str__(self):
        return '%s::%s' % (self.name, str(self.__class__.__name__))

    def __getitem__(self, arg):
        if isinstance(arg, int):
            return Module_with_ogate(self, arg)
        if isinstance(arg, slice):
            if arg.step is not None:
                assert False, 'Module does not get step arguement of slice'
            elif arg.stop is not None:
                assert False, 'Ingate is not yet implemented'
            elif arg.start is not None:
                return Module_with_ogate(self, arg)
            else:
                # default out gate is zero
                return Module_with_ogate(self, 0)
        else:
            assert False, 'invalid argument %s' % type(arg)

    def __add__(self, next_one):
        if isinstance(next_one, Module):
            self.connect(next_one)
        elif isinstance(next_one, Module_tuple):
            ogate = 0
            for module in next_one.mtuple:
                self.connect(module, gate = ogate)
                ogate += 1
        else:
            assert False, '%s is not a module or module_tuple' % next_one
        return next_one

    def connect(self, next_mod, gate = 0):
        if not isinstance(next_mod, Module):
            assert False, '%s is not a module' % next_mod

        #print 'Connecting %s[%d] -> %s' % (self.name, gate, next_mod.name)
        self.softnic.connect_modules(self.name, next_mod.name, gate)
        return next_mod     # for a->b->c syntax

    def query(self, arg = None, **kwargs):
        return self.softnic.query_module(self.name, 
                self.choose_arg(arg, kwargs))

class Module_with_ogate(Module):
    def __init__(self, m, ogate_id = 0):
        self.m = m
        self.ogate_id = ogate_id
    
    def __str__(self):
        return '%s::%s[%d]' % (self.name, str(self.__class__.__name__), \
                self.ogate_id)
    
    def __add__(self, next_one):
        self.m.connect(next_one, gate = self.ogate_id)
        return next_one

class Module_tuple():
    def __init__(self):
        self.mtuple = ()
        pass
    
    def __str__(self):
        s = '('
        for module in self.mtuple:
            s += '%s,' % module.name
        s += ')'
        return s

    def __add__(self, next_one):
        if isinstance(next_one, Module):
            assert False, 'Ingate is not yet implemented'
        elif isinstance(next_one, Module_tuple):
            assert False, 'Ingate is not yet implemented'
        else:
            assert False, '%s is not a module or module_tuple' % next_one

    def add_module(self, module):
        self.mtuple += (module,)
