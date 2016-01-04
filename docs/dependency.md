## Software Dependency

To build BESS on your Linux machine, you will need the following software packages (in Ubuntu package names; other Linux distros may have different package names).

NOTE: The provided build script `build.py` will automatically download and configure DPDK, so you don't need to install it yourself.

### Minimum Requirements
- git: to download BESS from the GitHub repository
- build-essential: for gcc, make, and libc-dev
- python: Python 2.7 required (Python 2.x/3.x support is TBD)
- libpcap-dev

There are other packages that are not strictly required, but strongly recommended as below.

### Optional Packages
- linux-header: to build the kernel module for BESS virtual ports
- graph-easy: "show pipeline" and "monitor pipeline" in bessctl depends on this Perl module
- docker: some sample configuration files use Docker, to daemonstrate how to use virtual ports in Linux containers.
- python-scapy: some sample configuration files use this library to craft test packets.

### Optional Packages to Enable DPDK PMDs
Some DPDK PMDs are not built due to external software dependencies. When the following packages are not available, the build script will warn you like this:
```
 - "zlib1g-dev" is not available. Disabling BNX2X PMD...
 - "Mellanox OFED" is not available. Disabling MLX4 and MLX5 PMDs...
```

- zlib1g-dev: for Broadcom BNX2X
- Mellanox OFED: for Mellanox MLX4 and MLX5. You can download it from [this link](http://www.mellanox.com/page/products_dyn?product_family=26&mtag=linux_sw_drivers). Note that `libmlx4/libmlx5` and `libibverbs` from the Ubuntu default repositories will *NOT* work.

After you install these packages, build BESS again:
```
$ ./build.py dist_clean
$ ./build.py
```
