sudo ip link del veth-host 2>/dev/null || true

sudo gdb ./bin/cni-plugin \
	-ex "set environment CNI_COMMAND ADD" \
	-ex "set environment CNI_CONTAINERID cnitool-77383ca0a0715733ca6f" \
	-ex "set environment CNI_NETNS /var/run/netns/testing" \
	-ex "set environment CNI_IFNAME eth0" \
	-ex "set environment CNI_PATH ./bin"

# (gdb) run < ./conf/conf.json
