#!/bin/bash
./scripts/clean_network.sh

sudo gdb ./sknf-cni/bin/sknf-cni \
	-ex "set environment CNI_COMMAND ADD" \
	-ex "set environment CNI_CONTAINERID cnitool-77383ca0a0715733ca6f" \
	-ex "set environment CNI_NETNS /var/run/netns/testing" \
	-ex "set environment CNI_IFNAME eth0" \
	-ex "set environment CNI_PATH ./sknf-cni/bin"

# (gdb) run < ./sknf-cni/conf/example-conf.json
