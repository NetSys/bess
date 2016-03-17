## Launching BESS

Once you have successfully built BESS, in the `bin/` directory you will find two executable files: `bessd` and `bessctl`. Consider adding the directory to your `PATH` environment variable for convenience.

### Before running

#### Adding your account to sudoers

The BESS daemon requires root privilege since it needs direct access to various hardware, such as physical memory, NIC ports, etc. You can launch the daemon either on a root account, or configure sudoers to grant sudo privilege to your account. The latter one is more recommended, and in most Linux distributions it is already done for the primary user account.

Optionally, you can allow sudo permission without requiring password. Run `visudo` and add this line (replace `accountname` with your account):

```
accountname ALL=NOPASSWD: ALL
```

#### Setting hugepages

BESS runs on top of DPDK, which requires hugepages for memory management. You can find details how to set up hugepages at [DPDK documentation](http://dpdk.org/doc/guides/linux_gsg/sys_reqs.html?highlight=hugepages#use-of-hugepages-in-the-linux-environment). BESS requires at least 2GB of memory reserved as hugepages. If you have NUMA systems, which have two processors or more, each node/processor/socket needs 2GB.

On recent x86 servers, 2MB and 1GB hugepages are supported. You can use either of them for BESS. For 2MB hugepages (at least 1024 pages are needed), you can simply run:

```
(on a single-socket system)
$ sudo sysctl -w vm.nr_hugepages=1024
```

or,

```
(on a NUMA system with two sockets)
$ echo 1024 | sudo tee /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
$ echo 1024 | sudo tee /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages
```

1GB hugepages (at least 2 pages are needed) can only be reserved at system boot time. Add `default_hugepagesz=1G hugepagesz=1G hugepages=2` to your kernel command line options. Multiply "2" by the number of NUMA nodes the system has. For example, if your server has two sockets and runs grub2, edit the `/etc/default/grub` file like this:

```
...
GRUB_CMDLINE_LINUX_DEFAULT="default_hugepagesz=1G hugepagesz=1G hugepages=4"
...
```

Then run `sudo update-grub` and reboot. Check `/proc/cmdline` to see if the change applied.

#### Using DPDK PMD for physical ports (optional)

> NOTE: You can skip this step if you are not planning to use DPDK physical ports.

BESS utilizes DPDK Poll-Mode Drivers (PMDs) to perform high-performance packet I/O with direct hardware NIC access. To use PMDs, first you need to unbind network interfaces from Linux kernel device drivers, then bind them to a [special module](http://dpdk.org/doc/guides/linux_gsg/build_dpdk.html?highlight=uio_pci_generic#loading-modules-to-enable-userspace-io-for-dpdk) (`uio_pci_generic`, `vfio-pci`, or `igb_uio`) to enable user-space I/O.

DPDK provides a script to (un)bind network interfaces. The following command with `--status` can be used to list all network interfaces in the system.

```sh
$ deps/dpdk-2.2.0/tools/dpdk_nic_bind.py --status

Network devices using DPDK-compatible driver
============================================
<none>

Network devices using kernel driver
===================================
0000:01:00.0 'MT27500 Family [ConnectX-3]' if=rename6,p2p1 drv=mlx4_core unused=
0000:03:00.0 'Ethernet Connection X552 10 GbE SFP+' if=eth0 drv=ixgbe unused=
0000:03:00.1 'Ethernet Connection X552 10 GbE SFP+' if=eth1 drv=ixgbe unused=
0000:07:00.0 'I210 Gigabit Network Connection' if=p5p1 drv=igb unused= *Active*
0000:08:00.0 'I210 Gigabit Network Connection' if=p6p1 drv=igb unused=

Other network devices
=====================
<none>
```

Suppose we want the Intel X552 dual-port NIC (03:00.0 and 03:00.1) to be used by BESS. These ports are currently used by the kernel `ixgbe` driver as `eth0` and `eth1`. The command below binds the ports to `uio_pci_generic` for user-space I/O.

```sh
$ sudo modprobe uio_pci_generic
$ sudo deps/dpdk-2.2.0/tools/dpdk_nic_bind.py -b uio_pci_generic 03:00.0 03:00.1
```

Then you can see the ports moved to under the "DPDK-compatible driver" section. The ports are ready to be used by BESS.

```sh
$ deps/dpdk-2.2.0/tools/dpdk_nic_bind.py --status

Network devices using DPDK-compatible driver
============================================
0000:03:00.0 'Ethernet Connection X552 10 GbE SFP+' drv=uio_pci_generic unused=
0000:03:00.1 'Ethernet Connection X552 10 GbE SFP+' drv=uio_pci_generic unused=

Network devices using kernel driver
===================================
0000:01:00.0 'MT27500 Family [ConnectX-3]' if=rename6,p2p1 drv=mlx4_core unused=uio_pci_generic
0000:07:00.0 'I210 Gigabit Network Connection' if=p5p1 drv=igb unused=uio_pci_generic *Active*
0000:08:00.0 'I210 Gigabit Network Connection' if=p6p1 drv=igb unused=uio_pci_generic

Other network devices
=====================
<none>
```

> NOTE: If you connect to a server with ssh connection, do not unbind the interface on which the connection is running.

### Running BESS daemon

`bessd` is the main BESS daemon process. It runs a packet processing datapath, while providing a control interface to external controllers. The process runs in background by default. There are two ways to launch the process. First, you can run the process directly.

```sh
# Run `bessd` directly. No command-line option is required
$ sudo bin/bessd
``` 

> NOTE: Only one instance of `bessd` process can run on a host. If there already is a running process, the new process will fail to run.

There are (optional) command options you can specify when running the daemon. You can combine multiple options if necessary.

* `-f`: The daemon runs in foreground mode.
  * This mode is useful for developers, in that log messages will be shown on the terminal.
  * BESS terminates once you break the process (`Ctrl+C`).
* '-d': The daemon runs in debug mode.
  * DEBUG level syslog messages will not be filtered.
  * In addition, all communication between a controller and the BESS daemon process will be dumped.
* '-k': If there already is a running BESS daemon process, kill it before launching a new one. Without this option, the new process will aborts with an error message.
* '-s': For every second, BESS prints out some statistics for each traffic class.

Alternatively, you can use the `bessctl` CLI interface. The details of `bessctl` can be found [here](bessctl.md). It will kill the old instance, if any.

```sh
# You don't need to be "root" to launch bessctl, but should be sudo-able.
$ bin/bessctl daemon start
```