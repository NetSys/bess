#!/usr/bin/env python
import glob
import os
import pkg_resources
import sys

from distutils.command.build_py import build_py as _build_py
from distutils.command.clean import clean as _clean
from setuptools import setup

PROTOBUF_DIR = 'protobuf'
PB2_OUTPUT_DIR = 'builtin_pb'

def generate_proto(source):
    """Invokes the Protocol Compiler to generate _pb2.py and _pb2_grpc.py
    from the given .proto file.  Does nothing if the output already exists
    and is newer than the input."""

    current_dir = (os.path.dirname(__file__) or os.curdir)

    output = os.path.join(
        current_dir,
        source.replace(".proto", "_pb2.py").replace(PROTOBUF_DIR, PB2_OUTPUT_DIR)
    )

    if (not os.path.exists(output) or
       (os.path.exists(source) and
       os.path.getmtime(source) > os.path.getmtime(output))):
        print("Generating %s..." % output)

        if not os.path.exists(source):
            sys.stderr.write("Can't find required file: %s\n" % source)
            sys.exit(-1)

        # Importing it here because it might not be yet installed when running
        # setup.py
        from grpc.tools import protoc
        proto_include = pkg_resources.resource_filename('grpc_tools', '_proto')
        command = [
            'grpc_tools.protoc',
            '-I{}'.format(proto_include),
            '--proto_path=protobuf',
            '--python_out={}/{}/'.format(current_dir, PB2_OUTPUT_DIR),
            '--grpc_python_out={}/{}/'.format(current_dir, PB2_OUTPUT_DIR),
        ] + [source]
        if protoc.main(command) != 0:
            sys.exit(-1)

class build_py(_build_py):
  def run(self):
    files = glob.glob(os.path.join(PROTOBUF_DIR, '*.proto'))
    files = files + glob.glob(os.path.join(PROTOBUF_DIR, 'ports', '*.proto'))
    for name in files:
        generate_proto(name)
    _build_py.run(self)

class clean(_clean):
    def run(self):
        for (dirpath, dirnames, filenames) in os.walk("."):
            for filename in filenames:
                filepath = os.path.join(dirpath, filename)
                if filepath.endswith("_pb2.py") or filepath.endswith("_pb2_grpc.py"):
                    os.remove(filepath)
        _clean.run(self)

with open("README.md", "r") as fh:
    long_description = fh.read()

setup(
    name='pybess_grpc',
    version='0.0.4-6',
    description='Python Bindings to Interact with BESS GRPC Daemon',
    long_description=long_description,
    long_description_content_type="text/markdown",
    author='Remi Vichery',
    author_email='remi.vichery@gmail.com',
    packages=[
        'pybess_grpc',
        'pybess_grpc.builtin_pb',
        'pybess_grpc.builtin_pb.ports',
        'pybess_grpc.plugin_pb',
        'pybess_grpc.plugin_pb.ports',
    ],
    package_dir={'pybess_grpc': '.'},
    package_data={
        'protobuf': [
            'protobuf/*.proto', 
            'protobuf/ports/*.proto', 
            'protobuf/tests/*.proto'
        ]
    },
    include_package_data=True,
    cmdclass={
        'clean': clean,
        'build_py': build_py
    },
    setup_requires=[
        "grpcio-tools==1.27.2",
        "grpcio==1.27.2",
        "protobuf==3.11.3"
    ],
    install_requires=[
        "grpcio==1.27.2",
        "protobuf==3.11.3",
    ],
)