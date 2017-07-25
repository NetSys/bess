# Copyright (c) 2014-2017, The Regents of the University of California.
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

import importlib
import glob
import os

__all__ = []

blacklist = ['builtin_pb.bess_msg_pb2', 'builtin_pb.port_msg_pb2']


def update_symbols(module, symbols):
    globals().update({name: module.__dict__[name] for name in symbols})


def import_module(module_name, update_symbol=True):
    module = importlib.import_module('..' + module_name, __name__)
    symbols = [n for n in module.__dict__ if
               n.endswith('Arg') or n.endswith('Response')]

    if update_symbol:
        update_symbols(module, symbols)


def import_package(package_name):
    cur_path = os.path.dirname(os.path.relpath(__file__))
    module_files = glob.glob(cur_path + '/' + package_name + '/*_msg_pb2.py')
    modules = [package_name + '.' + m
               for m in [os.path.basename(m)[:-3] for m in module_files]]

    for module_name in modules:
        if module_name not in blacklist:
            import_module(module_name)

import_package('builtin_pb')
import_package('plugin_pb')
import_module('builtin_pb.bess_msg_pb2', False)
