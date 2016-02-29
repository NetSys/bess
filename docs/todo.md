## TODO List

### Dataplane
* Metadata: Metadata can be used for modules to exchange useful per-packet information. The basic design can be found from this [tech report](https://www.eecs.berkeley.edu/Pubs/TechRpts/2015/EECS-2015-155.html).
* Timer: Timers will enable modules to schedule a task in the future.
* Input gate: Currently modules can have output gates only. Input gates can be used for diagnostics and flexible module functionality (e.g., processing packets differently, based on their input gates).
* Gate hooks: For each edge in the dataflow graph,

### Ports
* Non-polling mode: Currently BESS drivers only supports polling mode, i.e., worker threads consume 100% CPU cycles even if there is no pending packets. Non-polling mode will be supported for interrupt-enabled ports.
* (non-zerocopy) Native virtual ports: The current native virtual port directly connects BESS and user-level applications with kernel bypass. While this is done with zerocopy for optimal performance, we are also planning to support copy interface for fault isolation.

### Control interface
* C bindings
* Event channel: This will allow modules to notify external controllers for certain events (e.g., new flow arrival), synchronously or asynchronously.
* UNIX domain socket support

### Better multi-core support
* NUMA-aware data placement
* Per-core storage support for worker threads

### Built-in modules
* Inter-task queue (for multi-core pipelining)
* IPv4/v6 lookup modules
* Tunneling (e.g., VXLAN and NVGRE)
* OpenFlow switching modules
* Other useful modules: mirroring, link aggregation, NetFlow, etc.