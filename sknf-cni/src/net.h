#ifndef SKNF_NET_H
#define SKNF_NET_H

#include "err.h"

#define HOST_BRIDGE_NAME "brsknf"
#define HOST_VXLAN_NAME "vxsknf"
#define HOST_VETH_PREFIX "vethsknf-"

int net_attach_container(Err* err, const char* container_netns_name, const char* container_netif_name, const char* container_netif_cidr, const char* container_id, const char* bridge_cidr, const char* host_physical_if);
int net_detach_container(Err* err, const char* container_netns_name, const char* container_netif_name, const char* container_id);

#endif
