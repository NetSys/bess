## BESS (Berkeley Extensible Software Switch)

Detailed documentation will be available soon.

### Installation

```
git clone https://github.com/NetSys/bess.git
bess/build.py
```
Add `-b develop` when cloning if you want the most recent, but less stable version.

BESS runs on top of [DPDK](http://dpdk.org). The installation script will automatically download and build DPDK 2.0 in `deps/dpdk` directory. Like any other DPDK applications, you need to [set up hugepages](http://dpdk.org/doc/guides/linux_gsg/sys_reqs.html#reserving-hugepages-for-dpdk-use). If you want to use physical NIC ports, you also need to [bind ports to DPDK](http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html#binding-and-unbinding-network-ports-to-from-the-kernel-modules).

### Running BESS

In one terminal, run the BESS daemon as root:
```
sudo bess/bin/bessd
```

In another terminal, run the controller:
```
bess/bin/bessctl.py
```
