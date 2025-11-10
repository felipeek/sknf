#define _GNU_SOURCE
#include "net_utils.h"

#include <unistd.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/route/route.h>
#include <netlink/route/rtnl.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/link/vxlan.h>
#include <netlink/route/addr.h>
#include <netlink/addr.h>

#include "util.h"

// This function allocates an rtnl_addr (out) that must be released by the caller
int nu_rtnl_addr_build(Err* err, const char* cidr, int ifidx, struct rtnl_addr** out) {
	int rc = 1;
	int nl_err = 0;
	struct in_addr addr;
	int prefix;

	struct nl_addr* local = NULL;
	struct rtnl_addr* raddr = NULL;

	if (util_cidr_parse(err, cidr, &addr, &prefix)) {
		fprintf(stderr, "unable to parse CIDR %s\n", cidr);
		goto out;
	}

	char ip_only[INET_ADDRSTRLEN] = {0};
	if (!inet_ntop(AF_INET, &addr, ip_only, sizeof(ip_only))) {
		fprintf(stderr, "inet_ntop failed\n");
		ERR(err, "inet_ntop failed");
		goto out;
	}

	if ((nl_err = nl_addr_parse(ip_only, AF_INET, &local)) < 0) {
		fprintf(stderr, "nl_addr_parse failed for %s: %s\n", ip_only, nl_geterror(nl_err));
		ERRF(err, "Nl_addr_parse failed", "%s: %s", ip_only, nl_geterror(nl_err));
		goto out;
	}
	nl_addr_set_prefixlen(local, prefix);

	raddr = rtnl_addr_alloc();
	if (!raddr) {
		fprintf(stderr, "rtnl_addr_alloc failed\n");
		ERR(err, "Rtnl_addr_alloc failed");
		goto out;
	}

	rtnl_addr_set_family(raddr, AF_INET);
	rtnl_addr_set_scope(raddr, RT_SCOPE_UNIVERSE);
	rtnl_addr_set_local(raddr, local);
	rtnl_addr_set_ifindex(raddr, ifidx);

	*out = raddr;
	raddr = NULL;
	rc = 0;

out:
	if (local) nl_addr_put(local);
	if (raddr) rtnl_addr_put(raddr);
	return rc;
}

int nu_add_routing_rule(Err* err, struct nl_sock* sk,
							   const char* cidr, const char* next_ip,
							   int via_ifidx)
{
	int rc = 1;
	int nl_err = 0;

	struct rtnl_route *route = NULL;
	struct nl_addr *nl_addr_cidr = NULL;
	struct nl_addr *nl_addr_next_ip = NULL;
	struct rtnl_nexthop *nh = NULL;

	route = rtnl_route_alloc();
	if (!route) {
		fprintf(stderr, "failed to alloc rtnl_route\n");
		ERR(err, "Failed to alloc rtnl_route");
		goto out;
	}

	rtnl_route_set_family(route, AF_INET);
	rtnl_route_set_table(route, RT_TABLE_MAIN);
	rtnl_route_set_protocol(route, RTPROT_STATIC);
	rtnl_route_set_scope(route, RT_SCOPE_UNIVERSE);
	rtnl_route_set_type(route, RTN_UNICAST);

	if ((nl_err = nl_addr_parse(cidr, AF_INET, &nl_addr_cidr)) < 0) {
		fprintf(stderr, "nl_addr_parse failed for %s: %s\n", cidr, nl_geterror(nl_err));
		ERRF(err, "Nl_addr_parse failed for CIDR", "%s: %s", cidr, nl_geterror(nl_err));
		goto out;
	}

	if ((nl_err = nl_addr_parse(next_ip, AF_INET, &nl_addr_next_ip)) < 0) {
		fprintf(stderr, "nl_addr_parse failed for %s: %s\n", next_ip, nl_geterror(nl_err));
		ERRF(err, "Nl_addr_parse failed for next hop", "%s: %s", next_ip, nl_geterror(nl_err));
		goto out;
	}

	if ((nl_err = rtnl_route_set_dst(route, nl_addr_cidr)) < 0) {
		fprintf(stderr, "failed to set route dst: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failed to set route dst", "%s", nl_geterror(nl_err));
		goto out;
	}

	nh = rtnl_route_nh_alloc();
	if (!nh) {
		fprintf(stderr, "failed to alloc rtnl_nexthop\n");
		ERR(err, "Failed to alloc rtnl_nexthop");
		goto out;
	}

	rtnl_route_nh_set_ifindex(nh, via_ifidx);
	rtnl_route_nh_set_gateway(nh, nl_addr_next_ip);

	// After this call, the route owns 'nh'; no need to free 'nh'
	rtnl_route_add_nexthop(route, nh);
	nh = NULL;

	if ((nl_err = rtnl_route_add(sk, route, NLM_F_CREATE | NLM_F_REPLACE)) < 0) {
		fprintf(stderr, "failed to add route: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failed to add route", "%s", nl_geterror(nl_err));
		goto out;
	}

	rc = 0;

out:
	if (nh)			   rtnl_route_nh_free(nh);
	if (route)			rtnl_route_put(route);
	if (nl_addr_cidr)	 nl_addr_put(nl_addr_cidr);
	if (nl_addr_next_ip)  nl_addr_put(nl_addr_next_ip);
	return rc;
}

int nu_create_bridge(Err* err, struct nl_sock* sk, const char* bridge_cidr, const char* bridge_name) {
	int rc = 1;
	int nl_err = 0;

	int existing_ifidx = if_nametoindex(bridge_name);
	if (existing_ifidx != 0) {
		fprintf(stderr, "bridge already exists (ifidx=%d; name=%s)\n", existing_ifidx, bridge_name);
		return 0;
	}

	struct rtnl_link* bridge_link = NULL;
	struct rtnl_addr* raddr = NULL;

	bridge_link = rtnl_link_alloc();
	if (!bridge_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		ERR(err, "Failure allocating rtnl_link");
		goto out;
	}

	rtnl_link_set_name(bridge_link, bridge_name);
	rtnl_link_set_type(bridge_link, "bridge");
	rtnl_link_set_flags(bridge_link, IFF_UP);

	if ((nl_err = rtnl_link_add(sk, bridge_link, NLM_F_CREATE)) < 0) {
		fprintf(stderr, "failure creating bridge: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure creating bridge", "%s", nl_geterror(nl_err));
		goto out;
	}

	int ifidx = if_nametoindex(bridge_name);
	if (ifidx == 0) {
		fprintf(stderr, "failed to resolve ifindex for %s after creation\n", bridge_name);
		ERRF(err, "Failed to resolve ifindex for bridge", "%s", bridge_name);
		goto out;
	}

	if (nu_rtnl_addr_build(err, bridge_cidr, ifidx, &raddr)) {
		fprintf(stderr, "failure building bridge's rtnl_addr\n");
		goto out;
	}

	if ((nl_err = rtnl_addr_add(sk, raddr, NLM_F_CREATE | NLM_F_ACK)) < 0) {
		fprintf(stderr, "failed to assign cidr to bridge interface: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failed to assign cidr to bridge interface", "%s", nl_geterror(nl_err));
		goto out;
	}

	rc = 0;

out:
	if (raddr) rtnl_addr_put(raddr);
	if (bridge_link) rtnl_link_put(bridge_link);
	return rc;
}

int nu_create_vxlan(Err* err, struct nl_sock* sk, const char* underlay_if,
                    const char* vxlan_name, const char* vxlan_group, int vni_id)
{
    int rc = 1;
    int nl_err = 0;

    int existing_ifidx = if_nametoindex(vxlan_name);
    if (existing_ifidx != 0) {
        fprintf(stderr, "vxlan already exists (ifidx=%d)\n", existing_ifidx);
        return 0;
    }

    struct rtnl_link* vxlan_link = NULL;
    struct nl_addr* local = NULL;

    vxlan_link = rtnl_link_vxlan_alloc();
    if (!vxlan_link) {
        fprintf(stderr, "failure allocating vxlan rtnl_link\n");
        ERR(err, "Failure allocating vxlan rtnl_link");
        goto out;
    }

    if ((nl_err = nl_addr_parse(vxlan_group, AF_INET, &local)) < 0) {
        fprintf(stderr, "nl_addr_parse failed for %s: %s\n", vxlan_group, nl_geterror(nl_err));
        ERRF(err, "Nl_addr_parse failed", "%s: %s", vxlan_group, nl_geterror(nl_err));
        goto out;
    }

    rtnl_link_set_name(vxlan_link, vxlan_name);
    rtnl_link_vxlan_set_id(vxlan_link, vni_id);
    rtnl_link_vxlan_set_group(vxlan_link, local);
    rtnl_link_vxlan_set_port(vxlan_link, 4789);
    rtnl_link_set_flags(vxlan_link, IFF_UP);

    int ifindex = if_nametoindex(underlay_if);
    if (ifindex == 0) {
        fprintf(stderr, "failed to resolve ifindex for %s\n", underlay_if);
        ERRF(err, "Failed to resolve ifindex", "%s", underlay_if);
        goto out;
    }
    rtnl_link_vxlan_set_link(vxlan_link, ifindex);

    if ((nl_err = rtnl_link_add(sk, vxlan_link, NLM_F_CREATE)) < 0) {
        fprintf(stderr, "failure creating vxlan: %s\n", nl_geterror(nl_err));
        ERRF(err, "Failure creating vxlan", "%s", nl_geterror(nl_err));
        goto out;
    }

    rc = 0;

out:
    if (local) nl_addr_put(local);
    if (vxlan_link) rtnl_link_put(vxlan_link);
    return rc;
}

int nu_create_veth(Err* err, struct nl_sock* sk, int container_netns_fd,
                   const char* container_veth_name,
                   const char* container_veth_tmp_name,
                   const char* host_veth_name)
{
	int rc = 1;
	int nl_err = 0;

	struct rtnl_link* container_veth_link = NULL;
	struct rtnl_link* container_veth_changes_link = NULL;

	// Create veth pair
	// We give container's veth a temporary name to ensure it does not conflict with host interfaces
	if ((nl_err = rtnl_link_veth_add(sk, host_veth_name, container_veth_tmp_name, getpid())) < 0) {
		fprintf(stderr, "failure creating veth: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure creating veth", "%s", nl_geterror(nl_err));
		goto out;
	}

	int ifidx = if_nametoindex(container_veth_tmp_name);
	if (ifidx == 0) {
		fprintf(stderr, "failed to resolve ifindex for %s\n", container_veth_tmp_name);
		ERRF(err, "Failed to resolve ifindex for %s", "%s", container_veth_tmp_name);
		goto out;
	}

	// fetches a reference (rtnl_link) to container's veth interface from kernel
	if ((nl_err = rtnl_link_get_kernel(sk, ifidx, NULL, &container_veth_link)) < 0) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure filling rtnl_link information from kernel", "%s", nl_geterror(nl_err));
		goto out;
	}

	// create an rtnl_link to contain solely the desired change diff
	container_veth_changes_link = rtnl_link_alloc();
	if (!container_veth_changes_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		ERR(err, "Failure allocating rtnl_link");
		goto out;
	}

	// set interface; set the new desired network namespace (container's)
	rtnl_link_set_ifindex(container_veth_changes_link, ifidx);
	rtnl_link_set_ns_fd(container_veth_changes_link, container_netns_fd);
	rtnl_link_set_name(container_veth_changes_link, container_veth_name);
	rtnl_link_set_flags(container_veth_changes_link, IFF_UP);

	// apply changes (to change container's veth network namespace)
	if ((nl_err = rtnl_link_change(sk, container_veth_link, container_veth_changes_link, 0)) < 0) {
		fprintf(stderr, "failure moving veth to container network namespace: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure moving veth to container network namespace", "%s", nl_geterror(nl_err));
		goto out;
	}

	rc = 0;

out:
	if (container_veth_changes_link) rtnl_link_put(container_veth_changes_link);
	if (container_veth_link)         rtnl_link_put(container_veth_link);
	return rc;
}

int nu_enable_veth(Err* err, struct nl_sock* sk, const char* veth_name)
{
	int rc = 1;
	int nl_err = 0;

	struct rtnl_link* veth_link = NULL;
	struct rtnl_link* veth_changes_link = NULL;

	// fetches a reference (rtnl_link) to host's veth interface from kernel
	if ((nl_err = rtnl_link_get_kernel(sk, 0, veth_name, &veth_link)) < 0) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure filling rtnl_link information from kernel", "%s", nl_geterror(nl_err));
		goto out;
	}

	// create an rtnl_link to contain solely the desired change diff
	veth_changes_link = rtnl_link_alloc();
	if (!veth_changes_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		ERR(err, "Failure allocating rtnl_link");
		goto out;
	}

	rtnl_link_set_flags(veth_changes_link, IFF_UP);

	if ((nl_err = rtnl_link_change(sk, veth_link, veth_changes_link, 0)) < 0) {
		fprintf(stderr, "failure activating veth: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure activating veth", "%s", nl_geterror(nl_err));
		goto out;
	}

	rc = 0;

out:
	if (veth_changes_link) rtnl_link_put(veth_changes_link);
	if (veth_link)         rtnl_link_put(veth_link);
	return rc;
}

int nu_delete_if(Err* err, struct nl_sock* sk, const char* ifname) {
	int rc = 1;
	int nl_err = 0;

	// fetches a reference (rtnl_link) to host's veth interface from kernel
	struct rtnl_link* link = NULL;
	if ((nl_err = rtnl_link_get_kernel(sk, 0, ifname, &link)) < 0) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure filling rtnl_link information from kernel", "%s", nl_geterror(nl_err));
		goto out;
	}

	// delete the link (this also removes the peer end of the veth)
	if ((nl_err = rtnl_link_delete(sk, link)) < 0) {
		fprintf(stderr, "failure deleting veth pair %s: %s\n", ifname, nl_geterror(nl_err));
		ERRF(err, "Failure deleting veth pair", "%s: %s", ifname, nl_geterror(nl_err));
		goto out;
	}

	rc = 0;

out:
	if (link) rtnl_link_put(link);
	return rc;
}
