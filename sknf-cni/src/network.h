#ifndef SKNF_NETWORK_H
#define SKNF_NETWORK_H

#include "err.h"

#define HOST_BRIDGE_NAME "brsknf"
#define HOST_VXLAN_NAME "vxsknf"
#define HOST_VETH_PREFIX "vethsknf-"

int network_attach_container(Err* err, const char* container_netns_name, const char* container_netif_name, const char* container_netif_cidr, const char* container_id, const char* bridge_cidr);
int network_detach_container(Err* err, const char* container_netns_name, const char* container_netif_name, const char* container_id);

#endif
