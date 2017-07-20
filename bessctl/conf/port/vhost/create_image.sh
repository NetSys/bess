#!/bin/bash -e

# Copyright (c) 2014-2016, The Regents of the University of California.
# Copyright (c) 2016-2017, Nefeli Networks, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# * Neither the names of the copyright holders nor the names of their
# contributors may be used to endorse or promote products derived from this
# software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# We download a Vagrant box and convert into a qcow2 image...
BOX_URL=https://atlas.hashicorp.com/bento/boxes/ubuntu-14.04/versions/2.3.5/providers/virtualbox.box
curl -L $BOX_URL | tar zx ubuntu-14.04-amd64-disk001.vmdk

echo Converting image...
qemu-img convert -c -O qcow2 ubuntu-14.04-amd64-disk001.vmdk vm.qcow2
rm -f ubuntu-14.04-amd64-disk001.vmdk

# The default "insecure" key pair for Vagrant boxes.
# Do not expose this VM to the wild Internet.
KEY_URL=https://raw.githubusercontent.com/mitchellh/vagrant/master/keys/vagrant
rm -f vm.key
curl -L $KEY_URL > vm.key
chmod 400 vm.key

echo Done: image vm.qcow2 is ready. Now you can run launch_vm.sh
