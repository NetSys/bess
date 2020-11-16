
# pybess

`pybess` is a Python library which lets you interact with BESS using gRPC.

## Installation

You can install the library directly using pip/pip3:

```bash
pip3 install pybess_grpc
```

## Usage

As mentioned above `pybess` can be used to communicate with BESS over gRPC. 

An example of usage of this library is to list all BESS ports remotely:
```python
import sys

from google.protobuf.json_format import MessageToDict
from pybess_grpc.bess import BESS

bess = BESS()
try:
    bess.connect(grpc_url="%s:10514" % sys.argv[1])
    ports = bess.list_ports()
    print(MessageToDict(ports)["ports"])
finally:
    bess.disconnect()
```