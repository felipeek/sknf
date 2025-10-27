#!/bin/bash
./scripts/clean_network.sh

export CNI_COMMAND=ADD
export CNI_CONTAINERID=cnitool-77383ca0a0715733ca6f
export CNI_NETNS=/var/run/netns/testing
export CNI_IFNAME=eth0
export CNI_PATH=./sknf-cni/bin

./bin/sknf-cni < ./sknf-cni/conf/example-conf.json
