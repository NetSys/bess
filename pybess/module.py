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

import copy
import types


def _callback_factory(self, cmd, arg_type):
    return lambda mod, **kwargs: \
        self.bess.run_module_command(self.name, cmd, arg_type, kwargs)


class Module(object):

    def __init__(self, _do_not_create=False, **kwargs):
        self.name = '<uninitialized>'
        self.mclass = self.__class__.__name__

        assert self.__class__.__name__ != 'Module', \
            "Should not instantiate 'Module' directly"

        if 'name' in kwargs:
            name = kwargs['name']
            del kwargs['name']
        else:
            name = None

        if not _do_not_create:
            # create an object
            ret = self.bess.create_module(self.__class__.__name__, name,
                                          self.choose_arg(None, kwargs))
            self.name = ret.name
        else:
            # bind to a pre-existing object, check if it's real
            assert name is not None, "Module should not be None"
            info = self.bess.get_module_info(name)
            assert self.mclass == info.mclass, "Module %s is not of % type" % (
                name, self.mclass)
            self.name = name

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

    # Attach the task numbered `module_taskid` (usually modules only have one
    # task, numbered 0) from this module to a TC tree.
    #
    # The behavior differs based on the arguments provided:
    #
    # * If `wid` is specified, the task is attached as a root in the worker
    #   `wid`.  If `wid` has multiple roots they will be under a default
    #   round-robin policy.
    # * If `parent` is specified, the task is attached as a child of `parent`.
    #   If `parent` is a priority or weighted_fair TC, `priority` or `share`
    #   can be used to customize the child parameter.
    #
    def attach_task(self, parent='', wid=-1, module_taskid=0,
                    priority=None, share=None):
        return self.bess.attach_task(self.name, parent, wid, module_taskid,
                                     priority, share)
