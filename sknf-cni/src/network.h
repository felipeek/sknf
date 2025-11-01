#ifndef SKNF_NETWORK_H
#define SKNF_NETWORK_H

#define HOST_BRIDGE_NAME "brsknf"
#define HOST_VETH_PREFIX "vethsknf-"

int network_attach_container(const char* container_netns_name, const char* container_netif_name, const char* container_netif_cidr, const char* container_id);
int network_detach_container(const char* container_netns_name, const char* container_netif_name, const char* container_id);

#endif
