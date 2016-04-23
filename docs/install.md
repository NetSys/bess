## Installation

BESS runs on x86 64-bit Linux machines. After downloading BESS, simply run the build script `build.py`.

```
$ git clone https://github.com/NetSys/bess.git
$ bess/build.py
Downloading http://dpdk.org/browse/dpdk/snapshot/dpdk-2.2.0.tar.gz ... |
Decompressing DPDK...
Configuring DPDK...
Building DPDK...
Building BESS daemon...
Building BESS Linux kernel module... (optional)
Done.
$ _
```

There are some required software packages to build BESS, and optional packages to enable some extra features.

### Data Plane Development Kit (DPDK)

[DPDK](dpdk.org) is the most important software on which BESS is built. The build script will automatically download and configure DPDK, so **you do not need to install DPDK yourself**. More precisely, installing DPDK from other sources is not encouraged, since BESS may rely on some particular DPDK configurations.

If you do have good reasons to use your custom-built DPDK, set the `RTE_SDK` environment variable to your DPDK directory (see `core/Makefile`). This is only for advanced users.

### Software dependencies

To build BESS on your Linux machine, you will need the following software packages as minimum requirements.

- `git`: to download BESS from the GitHub repository
- `build-essential`: for gcc, make, and libc-dev
- `python`: Python 2.7 required (Python 3 support is planned)
- `libpcap-dev`

> NOTE: These package names are from Ubuntu. Other Linux distributions may have different package names.

There are other packages that are not strictly required, but strongly recommended as follows.

#### 1. Optional Packages
- `linux-headers`: to build the kernel module for BESS virtual ports. Linux 3.19 or higher is required.
- `libgraph-easy-perl`: "show pipeline" and "monitor pipeline" in bessctl depends on this Perl module
- `docker.io`: some sample configuration files use Docker, to demonstrate how to use virtual ports in Linux containers.
- `python-scapy`: some sample configuration files use this library to craft test packets.

#### 2. Optional packages to enable DPDK PMDs
Some DPDK Poll-Mode Drivers (PMDs) will not be built by default, due to external software dependencies. When the following packages are not available, the build script will warn you like this:

```
 - "zlib1g-dev" is not available. Disabling BNX2X PMD...
 - "Mellanox OFED" is not available. Disabling MLX4 and MLX5 PMDs...
```

- zlib1g-dev: for Broadcom BNX2X
- Mellanox OFED: for Mellanox MLX4 and MLX5. You can download it from [this link](http://www.mellanox.com/page/products_dyn?product_family=26&mtag=linux_sw_drivers). Note that `libmlx4/libmlx5` and `libibverbs` from the Ubuntu default repositories will *NOT* work.

After installing these packages, build BESS again:

```
$ ./build.py dist_clean
$ ./build.py
```