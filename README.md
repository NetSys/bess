[![Build Status](https://travis-ci.org/NetSys/bess.svg?branch=develop)](https://travis-ci.org/NetSys/bess)
[![codecov](https://codecov.io/gh/NetSys/bess/graph/badge.svg)](https://codecov.io/gh/NetSys/bess)

## BESS (Berkeley Extensible Software Switch)

BESS is a modular framework for software switches. BESS itself is *not* a virtual switch; it is neither pre-configured nor hardcoded to provide particular functionality, such as Ethernet bridging or OpenFlow-driven switching. Instead, you (or an external controller) can *configure* your own packet processing datapath by composing small "modules". While the basic concept is similar to [Click](http://read.cs.ucla.edu/click/click), BESS does not sacrifice performance for programmability.

BESS is developed at the University of California, Berkeley and at Nefeli Networks. [Contributors to BESS](https://github.com/NetSys/bess/blob/master/CONTRIBUTORS.md) include students, researchers, and developers who care about networking with high performance and high customizability. BESS is open-source under a BSD license.

If you are new to BESS, we recommend you start here:

1. [BESS Overview](https://github.com/NetSys/bess/wiki/BESS-Overview)
2. [Build and Install BESS](https://github.com/NetSys/bess/wiki/Build-and-Install-BESS)
3. [Write a BESS Configuration Script](https://github.com/NetSys/bess/wiki/Writing-a-BESS-Configuration-Script)
4. [Connect BESS to a Network Interface, VM, or Container](https://github.com/NetSys/bess/wiki/Hooking-up-BESS-Ports)

To configure and install BESS on Linux quickly, you can run the provided Ansible script (`vagrant/bess.yml`):

    git clone https://github.com/NetSys/bess.git
    cd bess/
    sudo apt-get install -y software-properties-common
    sudo apt-add-repository -y ppa:ansible/ansible
    sudo apt-get update
    sudo apt-get install -y ansible
    ansible-playbook vagrant/bess.yml
    sudo reboot
