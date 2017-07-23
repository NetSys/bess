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
