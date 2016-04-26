import types

def _callback_factory(self, cmd):
    return lambda mod, arg=None, **kwargs: \
        self.bess.run_module_command(self.name, cmd, 
                self.choose_arg(arg, kwargs))

class Module(object):
    def __init__(self, arg=None, **kwargs):
        self.name = '<uninitialized>'

        assert self.__class__.__name__ != 'Module', \
                "cannot instantiate 'Module'"

        if '_name' in kwargs:
            name = kwargs['_name']
            del kwargs['_name']
        else:
            name = None

        ret = self.bess.create_module(self.__class__.__name__, name, 
                self.choose_arg(arg, kwargs))

        self.name = ret.name
        #print 'Module %s created' % self

        # add mclass-specific methods
        cls = self.bess.get_mclass_info(self.__class__.__name__)
        for cmd in cls.commands:
            func = _callback_factory(self, cmd)
            setattr(self, cmd, types.MethodType(func, self))

        self.ogate = None
        self.igate = None

    def __str__(self):
        return '%s::%s' % (self.name, str(self.__class__.__name__))

    def __mul__(self, ogate):
        if not isinstance(ogate, int):
            assert False, 'Gate ID must be an integer'

        if self.ogate is not None:
            assert False, 'Output gate is already bound'

        self.ogate = ogate
        return self

    def __rmul__(self, igate):
        if not isinstance(igate, int):
            assert False, 'Gate ID must be an integer'

        if self.igate is not None:
            assert False, 'Input gate is already bound'

        self.igate = igate
        return self

    def __add__(self, next_mod):
        if not isinstance(next_mod, Module):
            assert False, '%s is not a module' % next_mod

        ogate = 0
        igate = 0

        if self.ogate is not None:
            ogate = self.ogate
            self.ogate = None

        if next_mod.igate is not None:
            igate = next_mod.igate
            next_mod.igate = None
        
        return self.connect(next_mod, ogate, igate)

    def connect(self, next_mod, ogate = 0, igate = 0):
        if not isinstance(next_mod, Module):
            assert False, '%s is not a module' % next_mod

        #print 'Connecting %s:%d -> %d:%s' % \
        #        (self.name, ogate, igate, next_mod.name)

        self.bess.connect_modules(self.name, next_mod.name, ogate, igate)
        
        # for a->b->c syntax
        return next_mod     
