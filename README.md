## BESS (Berkeley Extensible Software Switch)

Detailed documentation will be available soon.

### Installation

```
$ git clone https://github.com/NetSys/bess.git
$ bess/build.py
```

BESS runs on top of [DPDK](http://dpdk.org). The installation script will automatically download and build DPDK 2.0 in `deps/dpdk` directory. Like any other DPDK applications, you need to [set up hugepages](http://dpdk.org/doc/guides/linux_gsg/sys_reqs.html#reserving-hugepages-for-dpdk-use). If you want to use physical NIC ports, you also need to [bind ports to DPDK](http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html#binding-and-unbinding-network-ports-to-from-the-kernel-modules).

### Running BESS

Launch the BESS daemon as root, then you can control the dataplane with the controller, bessctl:
```
$ sudo bess/bin/bessd
$ bess/bin/bessctl.py
Type "help" for more information.
localhost:10514 $ _
```
