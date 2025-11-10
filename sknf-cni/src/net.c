#define _GNU_SOURCE
#include "net.h"

#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <errno.h>

#include <sched.h>

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
#include "net_utils.h"
#include "nft.h"

#define HOST_VXLAN_VNI_ID 100
#define HOST_VXLAN_GROUP "239.1.1.100"

static void generate_deterministic_host_if_name(Err* err, char buffer[16], const char* container_netns_name,
		const char* container_netif_name, const char* container_id) {
	unsigned h = 2166136261u; // FNV-1a offset basis
	h ^= util_fnv1a32(container_netns_name);
	h ^= util_fnv1a32(container_netif_name);
	h ^= util_fnv1a32(container_id);

	// Name pattern: 'v' + 8 hex chars (total 9 <= 15). Always NUL-terminated.
	snprintf(buffer, 16, "sknf%08x", h);
}

static void generate_deterministic_container_if_temporary_name(Err* err, char buffer[16], const char* container_netns_name,
		const char* container_netif_name, const char* container_id) {
	unsigned h = 2166136261u; // FNV-1a offset basis
	h ^= util_fnv1a32(container_netns_name);
	h ^= util_fnv1a32(container_netif_name);
	h ^= util_fnv1a32(container_id);

	// Name pattern: 'v' + 8 hex chars (total 9 <= 15). Always NUL-terminated.
	snprintf(buffer, 16, "tmp%08x", h);
}

static int configure_container_veth(Err* err, int container_netns_fd, const char* container_veth_name,
		const char* container_veth_cidr, const char* bridge_cidr) {
	int rc = 1;
	int nl_err = 0;
	int switched_ns = 0;

	int main_netns_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
	if (main_netns_fd < 0) {
		fprintf(stderr, "failure opening main net namespace fd: %s\n", strerror(errno));
		ERRF(err, "Failure opening main net namespace fd", "%s", strerror(errno));
		goto out;
	}

	if (setns(container_netns_fd, CLONE_NEWNET)) {
		fprintf(stderr, "failure associating thread to container's net ns: %s\n", strerror(errno));
		ERRF(err, "Failure associating thread to container's net ns", "%s", strerror(errno));
		goto out;
	}
	switched_ns = 1;

	// Create netlink socket in container's net ns
	struct nl_sock* sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		ERR(err, "Error allocating netlink socket");
		goto out;
	}
	if ((nl_err = nl_connect(sk, NETLINK_ROUTE)) < 0) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		ERRF(err, "Error creating/connecting to netlink socket", "%s", nl_geterror(nl_err));
		goto out;
	}

	int ifidx = if_nametoindex(container_veth_name);
	if (ifidx == 0) {
		fprintf(stderr, "failed to resolve ifindex for %s\n", container_veth_name);
		ERRF(err, "Failed to resolve ifindex for container veth", "%s", container_veth_name);
		goto out;
	}

	struct rtnl_link* link = NULL;
	// fetches a reference (rtnl_link) to container's veth interface from kernel
	if ((nl_err = rtnl_link_get_kernel(sk, ifidx, NULL, &link)) < 0) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure filling rtnl_link information from kernel", "%s", nl_geterror(nl_err));
		goto out;
	}

	struct rtnl_addr* raddr = NULL;
	if (nu_rtnl_addr_build(err, container_veth_cidr, ifidx, &raddr)) {
		fprintf(stderr, "failure building container's veth rtnl_addr\n");
		goto out;
	}

	if ((nl_err = rtnl_addr_add(sk, raddr, NLM_F_CREATE | NLM_F_ACK)) < 0) {
		fprintf(stderr, "failed to assign cidr to container's veth interface: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failed to assign cidr to container's veth interface", "%s", nl_geterror(nl_err));
		goto out;
	}

	if (nu_add_routing_rule(err, sk, "0.0.0.0/0", bridge_cidr, ifidx)) {
		fprintf(stderr, "failed to add default gateway to container's net ns\n");
		goto out;
	}

	if (setns(main_netns_fd, CLONE_NEWNET)) {
		fprintf(stderr, "failure re-associating thread to main net ns: %s\n", strerror(errno));
		ERRF(err, "Failure re-associating thread to main net ns", "%s", strerror(errno));
		goto out;
	}
	switched_ns = 0;

	rc = 0;

out:
	// Best-effort reset if we errored after switching namespaces
	if (switched_ns) {
		setns(main_netns_fd, CLONE_NEWNET);
	}

	if (raddr) rtnl_addr_put(raddr);
	if (link)  rtnl_link_put(link);
	if (sk) nl_socket_free(sk);

	if (main_netns_fd >= 0) close(main_netns_fd);
	return rc;
}

static int setup_veth(Err* err, struct nl_sock* sk, int container_netns_fd, const char* container_veth_name,
		const char* container_veth_tmp_name, const char* host_veth_name, const char* container_veth_cidr, const char* bridge_cidr) {
	if (nu_create_veth(err, sk, container_netns_fd, container_veth_name, container_veth_tmp_name, host_veth_name)) {
		fprintf(stderr, "failure creating veth\n");
		return 1;
	}

	if (configure_container_veth(err, container_netns_fd, container_veth_name, container_veth_cidr, bridge_cidr)) {
		fprintf(stderr, "failure configuring container's veth\n");
		return 1;
	}

	if (nu_enable_veth(err, sk, host_veth_name)) {
		fprintf(stderr, "failure creating veth\n");
		return 1;
	}

	return 0;
}

static int attach_ifs_to_bridge(Err* err, struct nl_sock* sk, const char* host_veth_name) {
	int rc = 1;
	int nl_err = 0;
	struct rtnl_link* bridge_link = NULL;
	struct rtnl_link* vxlan_link = NULL;
	struct rtnl_link* veth_link = NULL;
	struct rtnl_link* changes_link = NULL;

	// fetches a reference (rtnl_link) to the bridge interface from kernel
	if ((nl_err = rtnl_link_get_kernel(sk, 0, HOST_BRIDGE_NAME, &bridge_link)) < 0) {
		fprintf(stderr, "failure filling bridge information from kernel: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure filling bridge information from kernel", "%s", nl_geterror(nl_err));
		goto out;
	}
	
	// fetches a reference (rtnl_link) to the bridge interface from kernel
	if ((nl_err = rtnl_link_get_kernel(sk, 0, HOST_VXLAN_NAME, &vxlan_link)) < 0) {
		fprintf(stderr, "failure filling vxlan information from kernel: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure filling vxlan information from kernel", "%s", nl_geterror(nl_err));
		goto out;
	}

	// fetches a reference (rtnl_link) to host's veth interface from kernel
	if ((nl_err = rtnl_link_get_kernel(sk, 0, host_veth_name, &veth_link)) < 0) {
		fprintf(stderr, "failure filling host's veth information from kernel: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure filling host's veth information from kernel", "%s", nl_geterror(nl_err));
		goto out;
	}

	// create an rtnl_link to contain solely the desired change diff
	changes_link = rtnl_link_alloc();
	if (!changes_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		ERR(err, "Failure allocating rtnl_link");
		goto out;
	}

	// fetches a reference (rtnl_link) to vxlan interface from kernel
	rtnl_link_set_ifindex(changes_link, if_nametoindex(HOST_VXLAN_NAME));
	rtnl_link_set_master(changes_link, rtnl_link_get_ifindex(bridge_link));
	if ((nl_err = rtnl_link_change(sk, vxlan_link, changes_link, 0)) < 0) {
		fprintf(stderr, "failure enslaving vxlan to bridge: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure enslaving vxlan to bridge", "%s", nl_geterror(nl_err));
		goto out;
	}

	// fetches a reference (rtnl_link) to veth interface from kernel
	rtnl_link_set_ifindex(changes_link, if_nametoindex(host_veth_name));
	rtnl_link_set_master(changes_link, rtnl_link_get_ifindex(bridge_link));
	if ((nl_err = rtnl_link_change(sk, veth_link, changes_link, 0)) < 0) {
		fprintf(stderr, "failure enslaving host's veth to bridge: %s\n", nl_geterror(nl_err));
		ERRF(err, "Failure enslaving host's veth to bridge", "%s", nl_geterror(nl_err));
		goto out;
	}

	rc = 0;

out:
	if (changes_link) rtnl_link_put(changes_link);
	if (veth_link)    rtnl_link_put(veth_link);
	if (vxlan_link)   rtnl_link_put(vxlan_link);
	if (bridge_link)  rtnl_link_put(bridge_link);
	return rc;
}

int net_attach_container(Err* err, const char* container_netns_name, const char* container_netif_name,
		const char* container_netif_cidr, const char* container_id, const char* bridge_cidr, const char* host_physical_if) {
	int rc = 1;
	int nl_err = 0;

	// Create netlink socket
	struct nl_sock* sk = NULL;
	int container_netns_fd = -1;

	sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		ERR(err, "Error allocating netlink socket");
		goto out;
	}
	if ((nl_err = nl_connect(sk, NETLINK_ROUTE)) < 0) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		ERRF(err, "Error creating/connecting to netlink socket", "%s", nl_geterror(nl_err));
		goto out;
	}

	// file descriptor for the target network namespace
	container_netns_fd = open(container_netns_name, O_RDONLY | O_CLOEXEC);
	if (container_netns_fd < 0) {
		fprintf(stderr, "failure opening target net namespace fd: %s\n", strerror(errno));
		ERRF(err, "Failure opening target net namespace fd", "%s", strerror(errno));
		goto out;
	}

	char host_if_name[16];
	char container_if_tmp_name[16];
	generate_deterministic_host_if_name(err, host_if_name, container_netns_name, container_netif_name, container_id);
	generate_deterministic_container_if_temporary_name(err, container_if_tmp_name, container_netns_name, container_netif_name, container_id);

	if (nu_create_bridge(err, sk, bridge_cidr, HOST_BRIDGE_NAME)) {
		fprintf(stderr, "failure creating bridge\n");
		goto out;
	}

	if (nu_create_vxlan(err, sk, host_physical_if, HOST_VXLAN_NAME, HOST_VXLAN_GROUP, HOST_VXLAN_VNI_ID)) {
		fprintf(stderr, "failure creating vxlan\n");
		goto out;
	}

	if (setup_veth(err, sk, container_netns_fd, container_netif_name, container_if_tmp_name, host_if_name, container_netif_cidr, bridge_cidr)) {
		fprintf(stderr, "failure creating veth\n");
		goto out;
	}

	if (attach_ifs_to_bridge(err, sk, host_if_name)) {
		fprintf(stderr, "failure attaching veth to bridge\n");
		goto out;
	}

	// create nftables NAT rule to ensure packets leaving the cluster are NAT'd with host's physical IP as SRC IP
	// (to ensure response is routable)
	if (nft_nat_rule(err, host_physical_if, container_netif_cidr)) {
		fprintf(stderr, "failure creating nft NAT rule\n");
		goto out;
	}

	rc = 0;

out:
	if (container_netns_fd >= 0) close(container_netns_fd);
	if (sk) nl_socket_free(sk);
	return rc;
}

int net_detach_container(Err* err, const char* container_netns_name, const char* container_netif_name, const char* container_id) {
	int rc = 1;
	int nl_err = 0;

	// Create netlink socket
	struct nl_sock* sk = NULL;

	sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		ERR(err, "Error allocating netlink socket");
		goto out;
	}
	if ((nl_err = nl_connect(sk, NETLINK_ROUTE)) < 0) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		ERRF(err, "Error creating/connecting to netlink socket", "%s", nl_geterror(nl_err));
		goto out;
	}

	char host_veth_name[16];
	generate_deterministic_host_if_name(err, host_veth_name, container_netns_name, container_netif_name, container_id);

	if (nu_delete_if(err, sk, host_veth_name)) {
		fprintf(stderr, "failure deleting interface %s\n", host_veth_name);
		goto out;
	}

	rc = 0;

out:
	if (sk) nl_socket_free(sk);
	return rc;
}