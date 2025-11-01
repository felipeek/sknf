#!/bin/bash
./scripts/clean_network.sh
sudo env "PATH=$PATH" NETCONFPATH=$(realpath ./sknf-cni/conf) CNI_PATH=./sknf-cni/bin cnitool add sknf-network-example /var/run/netns/testing
sudo env "PATH=$PATH" NETCONFPATH=$(realpath ./sknf-cni/conf) CNI_PATH=./sknf-cni/bin cnitool add sknf-network-example /var/run/netns/testing2
