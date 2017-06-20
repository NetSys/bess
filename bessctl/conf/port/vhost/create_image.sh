#!/bin/bash -e

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
