#ifndef SKNF_NET_UTILS_H
#define SKNF_NET_UTILS_H

#include <netlink/route/link.h>
#include <netlink/route/addr.h>
#include "err.h"

int nu_rtnl_addr_build(Err* err, const char* cidr, int ifidx, struct rtnl_addr** out);
int nu_add_routing_rule(Err* err, struct nl_sock* sk, const char* cidr, const char* next_ip, int via_ifidx);
int nu_create_bridge(Err* err, struct nl_sock* sk, const char* bridge_cidr, const char* bridge_name);
int nu_create_vxlan(Err* err, struct nl_sock* sk, const char* underlay_if,
                    const char* vxlan_name, const char* vxlan_group, int vni_id);
int nu_create_veth(Err* err, struct nl_sock* sk, int container_netns_fd,
                   const char* container_veth_name,
                   const char* container_veth_tmp_name,
                   const char* host_veth_name);
int nu_enable_veth(Err* err, struct nl_sock* sk, const char* veth_name);
int nu_delete_if(Err* err, struct nl_sock* sk, const char* ifname);

#endif