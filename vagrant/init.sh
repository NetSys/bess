#!/bin/sh

cp /opt/bess/vagrant/files/sysctl.conf /etc/sysctl.conf
cp /opt/bess/vagrant/files/environment /etc/environment
cp /opt/bess/vagrant/files/rc.local /etc/rc.local

sysctl -p
mkdir -p /mnt/huge
adduser --quiet vagrant adm
adduser --quiet vagrant docker
sudo -u vagrant ln -f -s /opt/bess ~vagrant/bess
