sudo ip link del veth-host
sudo env "PATH=$PATH" NETCONFPATH=$(realpath ./conf) CNI_PATH=./bin cnitool add mynet /var/run/netns/testing
