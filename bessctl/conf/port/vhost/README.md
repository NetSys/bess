### Requirements

* Install these packages:
  * numactl
  * qemu-kvm
* Reserve at least 3GB of hugepages. 4GB if running on a NUMA machine
  * Each VM requires 2GB. BESS takes 1GB per socket.
  * On NUMA, check each socket has enough free hugepages.
  * It is recommended to use 1GB hugepages rather than 2MB ones.


### vhost.bess

This BESS script creates `BESS_VMS` VMs, each of which has `BESS_PORTS` Virtio
ports. Each port has `BESS_QUEUES` RX/TX queue pairs. The datapath is simple:
* BESS generates packets and sent them to all RX queues of VMs
  * The size of packets can be adjusted with `BESS_PKT_SIZE`
* BESS receives from all TX queues of VMs and discards them.
* With `monitor port` you can see the throughput of this datapath.
  * Normally vhost side is the performance bottleneck.


### create\_image.sh

This script generates a vm.qcow2 image file that can be used with QEMU.
The image is based on Ubuntu 14.04. You must create an image before launching
VMs below.


### launch\_vm.py

You can launch one or multiple VMs. Each VM runs `testpmd` that simply swaps
source/destination MAC addresses of packets and forward them from one port to
another. You must launch `vhost.bess` first, so that QEMU can connect to BESS
vport sockets. Otherwise you will see an error like this:
`qemu-system-x86_64: ... Failed to connect socket: No such file or directory`

Environment variables:
* `VM_START_CPU`: Default: 1
* `VM_VCPUS`: # of virtual cores per VM. Default: 2
  * One core is reserved for background processes.
  * The other N-1 cores will be used for forwarding
* `VM_MEM_SOCKET`: Default: 0
* `HUGEPAGES_PATH`: Default: /dev/hugepages
* `FWD_MODE`: Default: "macswap retry"
  * The forwarding mode of the 
  * See [DPDK User guide](http://dpdk.org/doc/guides/testpmd_app_ug/index.html)
* `BESS_PORTS`: # of virtio ports per VM. Default: 2
* `BESS_QUEUES`: # of RX/TX queue pairs per port. Default: 1
* `VERBOSE`: Default: 0
