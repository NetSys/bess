This directory contains various scripts to help users start trying BESS without
much pain. There is a Vagrant script that allows you to bring up a VM that is
pre-configured with all required dependencies. There are also Ansible scripts in
case you want to build and run BESS on a native Linux environment, either your
laptop or server.

## Playing with BESS VM

You can launch a fully conigured VM on which you can build and test BESS at your
fingertips. We don't provide a binary VM image though. Instead, We provide a
Vagrant (https://vagrantup.com) script that automatically generates a VM based
on Ubuntu 18.04. You can install Vagrant not only on Linux, but also on Windows
or macOS. Once you have `vagrant` installed, in the current (env/) directory,
you can simply run:

```sh
$ vagrant up
```

to launch a VM. The current BESS directory is mapped to `/opt/bess` in the VM. 
You can connect to the VM with `vagrant ssh`. All compilers and libraries are
readiliy available.

## Building BESS without installing dependencies

If you want to do something more serious than playing within a sandbox VM, but
still without getting your hands dirty, you can use our Docker container to
build BESS. The container is, similarly to the Vagrant VM, configured with all
software packages required by BESS. With Docker available, just run (in the top
directory):

```sh
$ ./container_build.py
```

then the script will automatically fetch the container image
(nefelinetworks/bess_build at hub.docker.com) and build BESS inside the
container. Since the BESS binary is mostly static-linked to external libraries,
the binary built in the container should be readily runnable in the host as
well.

## Ansible scripts

If you plan to build BESS from source and test it without using a VM or
a container, you must install required packages on your Linux machine. There are
various Ansible script files you can use for those dependencies.

```sh
$ ./ansible-playbook -K -i localhost, -c local <YAML script>
```

Replace `<YAML script>` with one of the following with different flavors:

* `env/runtime.yml`: In case you already have a compiled binary of BESS, this
    file includes software packages for the runtime. It also configures
    hugepages.
* `env/build-dep.yml`: This file contains minimum software requirements for 
    building BESS.
* `env/dev.yml`: This script has all packages included in the both files above,
    also with some optional yet recommended packages for developers.
