#ifndef SKNF_NETWORK_H
#define SKNF_NETWORK_H

#define HOST_BRIDGE_NAME "brsknf"
#define HOST_VETH_PREFIX "vethsknf-"

int network_setup(const char* container_netns_name, const char* container_netif_name);

#endif
