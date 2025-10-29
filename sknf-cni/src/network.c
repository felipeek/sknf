#include "network.h"

#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <errno.h>

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>

#include "util.h"

static void generate_deterministic_if_name(
	char buffer[16],
	const char* container_netns_name,
	const char* container_netif_name,
	const char* container_id
) {
	unsigned h = 2166136261u; // FNV-1a offset basis
	h ^= util_fnv1a32(container_netns_name);
	h ^= util_fnv1a32(container_netif_name);
	h ^= util_fnv1a32(container_id);

	// Name pattern: 'v' + 8 hex chars (total 9 <= 15). Always NUL-terminated.
	snprintf(buffer, 16, "v%08x", h);
}

static int create_bridge(struct nl_sock* sk) {
	int nl_err;

	// create an rtnl_link to contain solely the desired change diff
	struct rtnl_link* bridge_link;
	bridge_link = rtnl_link_alloc();
	if (!bridge_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		// todo: free resources
		return 1;
	}

	rtnl_link_set_name(bridge_link, HOST_BRIDGE_NAME);
	rtnl_link_set_type(bridge_link, "bridge");

	if (nl_err = rtnl_link_add(sk, bridge_link, NLM_F_CREATE)) {
		fprintf(stderr, "failure creating bridge: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	return 0;
}

static int create_veth(struct nl_sock* sk, int container_netns_fd, const char* container_veth_name, const char* host_veth_name) {
	int nl_err;

	// Create veth pair
	if (nl_err = rtnl_link_veth_add(sk, host_veth_name, container_veth_name, getpid())) {
		fprintf(stderr, "failure creating veth: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	int ifidx = if_nametoindex(container_veth_name);
	struct rtnl_link* peer_link;
	struct rtnl_link* changes_link;

	// fetches a reference (rtnl_link) to container's veth interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, ifidx, NULL, &peer_link)) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	// create an rtnl_link to contain solely the desired change diff
	changes_link = rtnl_link_alloc();
	if (!changes_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		// todo: free resources
		return 1;
	}

	// set interface; set the new desired network namespace (container's)
	rtnl_link_set_ifindex(changes_link, ifidx);
	rtnl_link_set_ns_fd(changes_link, container_netns_fd);

	// apply changes (to change container's veth network namespace)
	if (nl_err = rtnl_link_change(sk, peer_link, changes_link, 0)) {
		fprintf(stderr, "failure moving veth to container network namespace: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	rtnl_link_put(peer_link);
	rtnl_link_put(changes_link);
	return 0;
}

static int attach_host_veth_to_bridge(struct nl_sock* sk, const char* host_veth_name) {
	int nl_err;
	struct rtnl_link* bridge_link;
	struct rtnl_link* veth_link;
	struct rtnl_link* changes_link;

	// fetches a reference (rtnl_link) to the bridge interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, 0, HOST_BRIDGE_NAME, &bridge_link)) {
		fprintf(stderr, "failure filling bridge information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	// fetches a reference (rtnl_link) to host's veth interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, 0, host_veth_name, &veth_link)) {
		fprintf(stderr, "failure filling host's veth information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	// create an rtnl_link to contain solely the desired change diff
	changes_link = rtnl_link_alloc();
	if (!changes_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		// todo: free resources
		return 1;
	}

	// fetches a reference (rtnl_link) to veth interface from kernel
	rtnl_link_set_ifindex(changes_link, if_nametoindex(host_veth_name));
	rtnl_link_set_master(changes_link, rtnl_link_get_ifindex(bridge_link));
	if (rtnl_link_change(sk, veth_link, changes_link, 0)) {
		fprintf(stderr, "failure enslaving host's veth to bridge: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	return 0;
}

int network_attach_container(const char* container_netns_name, const char* container_netif_name, const char* container_id) {
	int nl_err;

	// Create netlink socket
	struct nl_sock* sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		// todo: free resources
		return 1;
	}
	if (nl_err = nl_connect(sk, NETLINK_ROUTE)) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	// file descriptor for the target network namespace
	int container_netns_fd = open(container_netns_name, O_RDONLY | O_CLOEXEC);
	if (container_netns_fd < 0) {
		fprintf(stderr, "failure opening target net namespace fd: %s\n", strerror(errno));
		// todo: free resources
		return 1;
	}

	char host_veth_name[16];
	generate_deterministic_if_name(host_veth_name, container_netns_name, container_netif_name, container_id);

	if (create_bridge(sk)) {
		fprintf(stderr, "failure creating bridge\n");
		// todo: free resources
		return 1;
	}

	if (create_veth(sk, container_netns_fd, container_netif_name, host_veth_name)) {
		fprintf(stderr, "failure creating veth\n");
		// todo: free resources
		return 1;
	}

	if (attach_host_veth_to_bridge(sk, host_veth_name)) {
		fprintf(stderr, "failure attaching veth to bridge\n");
		// todo: free resources
		return 1;
	}

	close(container_netns_fd);
	nl_socket_free(sk);
	return 0;
}

int network_detach_container(const char* container_netns_name, const char* container_netif_name, const char* container_id) {
	int nl_err;

	// Create netlink socket
	struct nl_sock* sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		// todo: free resources
		return 1;
	}
	if (nl_err = nl_connect(sk, NETLINK_ROUTE)) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	char host_veth_name[16];
	generate_deterministic_if_name(host_veth_name, container_netns_name, container_netif_name, container_id);

	// fetches a reference (rtnl_link) to host's veth interface from kernel
	struct rtnl_link* link;
	if (nl_err = rtnl_link_get_kernel(sk, 0, host_veth_name, &link)) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	// delete the link (this also removes the peer end of the veth)
	if (nl_err = rtnl_link_delete(sk, link) < 0) {
		fprintf(stderr, "failure deleting veth pair %s: %s\n", host_veth_name, nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	rtnl_link_put(link);
	nl_socket_free(sk);
	return 0;
}