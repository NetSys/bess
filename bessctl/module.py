import copy
import types


def _callback_factory(self, cmd, arg_type):
    return lambda mod, **kwargs: \
        self.bess.run_module_command(self.name, cmd, arg_type, kwargs)


class Module(object):
    def __init__(self, **kwargs):
        self.name = '<uninitialized>'
        self.mclass = self.__class__.__name__

        assert self.__class__.__name__ != 'Module', \
            "Should not instantiate 'Module' directly"

        if 'name' in kwargs:
            name = kwargs['name']
            del kwargs['name']
        else:
            name = None

        ret = self.bess.create_module(self.__class__.__name__, name,
                                      self.choose_arg(None, kwargs))

        self.name = ret.name

        # add mclass-specific methods
        cls = self.bess.get_mclass_info(self.__class__.__name__)
        assert len(cls.cmds) == len(cls.cmd_args)
        for i, cmd in enumerate(cls.cmds):
            func = _callback_factory(self, cmd, cls.cmd_args[i])
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

        ret = copy.copy(self)
        ret.ogate = ogate
        return ret

    def __rmul__(self, igate):
        if not isinstance(igate, int):
            assert False, 'Gate ID must be an integer'

        if self.igate is not None:
            assert False, 'Input gate is already bound'

        ret = copy.copy(self)
        ret.igate = igate
        return ret

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

    def connect(self, next_mod, ogate=0, igate=0):
        if not isinstance(next_mod, Module):
            assert False, '%s is not a module' % next_mod

        self.bess.connect_modules(self.name, next_mod.name, ogate, igate)

        # for a->b->c syntax
        return next_mod
