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

import importlib
import glob
import os

exclude_list = ['bess_msg_pb2', 'port_msg_pb2']


def _load_symbols(mod, *symbols):
    globals().update({name: mod.__dict__[name] for name in symbols})


def load_symbol(mod_name, symbol):
    mod = importlib.import_module('..' + mod_name, __name__)
    _load_symbols(mod, symbol)


def load_symbols(mod_name):
    mod = importlib.import_module('..' + mod_name, __name__)
    symbols = [n for n in mod.__dict__ if
               n.endswith('Arg') or n.endswith('Response')]
    _load_symbols(mod, *symbols)


dir_path = os.path.dirname(os.path.realpath(__file__))
mod_files = glob.glob(dir_path + '/*_msg_pb2.py')
mods = [m for m in [os.path.basename(m)[:-3] for m in mod_files]
        if m not in exclude_list]

for mod_name in mods:
    load_symbols(mod_name)

load_symbol('bess_msg_pb2', 'EmptyArg')
