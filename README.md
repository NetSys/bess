## BESS (Berkeley Extensible Software Switch)

BESS is a modular framework for software switches. BESS itself is *not* a virtual switch; it is neither pre-configured or hardcoded to perform particular functionality, such as Ethernet bridging or OpenFlow switching. Instead, you (or an external controller) can *configure* your own packet processing datapath by composing small "modules". While the basic concept is similar to [Click](http://read.cs.ucla.edu/click/click), BESS does not sacrifice performance for programmability.

Detailed documentation will be available **soon**.

### Installation

First, make sure that your Linux machine has all [required packages](docs/dependency.md) installed. After that you can simply clone the repository and run the build script. If there is any missing package, the script will tell you so.

```
$ git clone https://github.com/NetSys/bess.git
$ bess/build.py
```

BESS runs on top of [DPDK](http://dpdk.org). The installation script will automatically download and build DPDK 2.2 in `deps/dpdk-2.2.0` directory. 

### Running BESS

Like any other DPDK applications, you need to [set up hugepages](http://dpdk.org/doc/guides/linux_gsg/sys_reqs.html#reserving-hugepages-for-dpdk-use) at least 1GB for each CPU socket. Using 2MB hugepages is recommended since it can be configured without system reboot and the performance difference is negligible. 

If you want to use physical NIC ports (as an exception, you can skip this step for Mellanox NICs), you also need to [bind ports to DPDK](http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html#binding-and-unbinding-network-ports-to-from-the-kernel-modules):

```
$ sudo modprobe uio_pci_generic
$ sudo bess/deps/dpdk-2.2.0/tools/dpdk_nic_bind.py -b uio_pci_generic PCI_DEV1 [PCI_DEV2 ...]
```

You can search for the PCI device IDs (in xx:yy.z form) corresponding to the physical ports you wish to bind by running

```
$ bess/deps/dpdk-2.2.0/tools/dpdk_nic_bind.py --status
```

Once ready to roll, launch the BESS daemon as root, then you can control the dataplane with the controller, `bessctl`:

```
$ sudo bess/bin/bessd
$ bess/bin/bessctl
Type "help" for more information.
localhost:10514 $ _
```
