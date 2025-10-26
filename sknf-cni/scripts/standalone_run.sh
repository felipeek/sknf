ip link del veth-host 2>/dev/null || true

export CNI_COMMAND=ADD
export CNI_CONTAINERID=cnitool-77383ca0a0715733ca6f
export CNI_NETNS=/var/run/netns/testing
export CNI_IFNAME=eth0
export CNI_PATH=./bin

./bin/sknf-cni < ./conf/conf.json
