import importlib
import glob
import os

exclude_list = ['bess_msg_pb2', 'port_msg_pb2']


def load_symbols(mod_name):
    mod = importlib.import_module(mod_name)
    symbols = [n for n in mod.__dict__ if
               n.endswith('Arg') or n.endswith('Response')]
    globals().update({name: mod.__dict__[name] for name in symbols})


dir_path = os.path.dirname(os.path.abspath(__file__))
mod_files = glob.glob(dir_path + '/*_msg_pb2.py')
mods = filter(lambda m: m not in exclude_list,
              [os.path.basename(m)[:-3] for m in mod_files])

for mod_name in mods:
    load_symbols(mod_name)
