#define _GNU_SOURCE
#include "network.h"

#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <errno.h>

#include <sched.h>

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>
#include <netlink/route/addr.h>
#include <netlink/addr.h>

#include "util.h"

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

static int create_bridge(Err* err, struct nl_sock* sk) {
	int nl_err;

	// create an rtnl_link to contain solely the desired change diff
	struct rtnl_link* bridge_link;
	bridge_link = rtnl_link_alloc();
	if (!bridge_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		// todo: free resources
		ERR(err, "Failure allocating rtnl_link");
		return 1;
	}

	rtnl_link_set_name(bridge_link, HOST_BRIDGE_NAME);
	rtnl_link_set_type(bridge_link, "bridge");

	if (nl_err = rtnl_link_add(sk, bridge_link, NLM_F_CREATE)) {
		fprintf(stderr, "failure creating bridge: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure creating bridge", "%s", nl_geterror(nl_err));
		return 1;
	}

	return 0;
}

static int configure_container_veth(Err* err, int container_netns_fd, const char* container_veth_name,
		const char* container_veth_cidr) {
	int nl_err;

	int main_netns_fd = open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
	if (main_netns_fd < 0) {
		fprintf(stderr, "failure opening main net namespace fd: %s\n", strerror(errno));
		// todo: free resources
		ERRF(err, "Failure opening main net namespace fd", "%s", strerror(errno));
		return 1;
	}

	if (setns(container_netns_fd, CLONE_NEWNET)) {
		fprintf(stderr, "failure associating thread to container's net ns: %s\n", strerror(errno));
		// todo: free resources
		ERRF(err, "Failure associating thread to container's net ns", "%s", strerror(errno));
		return 1;
	}

	// Create netlink socket in container's net ns
	struct nl_sock* sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		// todo: free resources
		ERR(err, "Error allocating netlink socket");
		return 1;
	}
	if (nl_err = nl_connect(sk, NETLINK_ROUTE)) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Error creating/connecting to netlink socket", "%s", nl_geterror(nl_err));
		return 1;
	}

	int ifidx = if_nametoindex(container_veth_name);
	struct rtnl_link* link;

	// fetches a reference (rtnl_link) to container's veth interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, ifidx, NULL, &link)) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure filling rtnl_link information from kernel", "%s", nl_geterror(nl_err));
		return 1;
	}

	const char* slash = strchr(container_veth_cidr, '/');
	if (!slash) {
		fprintf(stderr, "invalid CIDR (missing /): %s\n", container_veth_cidr);
		// todo: free resources
		ERRF(err, "Invalid CIDR (missing /)", "%s", container_veth_cidr);
		return 1;
	}

	char ip_only[INET_ADDRSTRLEN];
	size_t ip_len = (size_t)(slash - container_veth_cidr);
	if (ip_len >= sizeof(ip_only)) {
		fprintf(stderr, "invalid CIDR (IP too long): %s\n", container_veth_cidr);
		// todo: free resources
		ERRF(err, "Invalid CIDR (IP too long)", "%s", container_veth_cidr);
		return 1;
	}
	memcpy(ip_only, container_veth_cidr, ip_len);
	ip_only[ip_len] = '\0';

	char* endp = NULL;
	long pref = strtol(slash + 1, &endp, 10);
	if (*endp != '\0' || pref < 0 || pref > 32) {
		fprintf(stderr, "invalid CIDR prefix: %s\n", container_veth_cidr);
		// todo: free resources
		ERRF(err, "Invalid CIDR prefix", "%s", container_veth_cidr);
		return 1;
	}

	struct nl_addr* local = NULL;
	if ((nl_err = nl_addr_parse(ip_only, AF_INET, &local))) {
		fprintf(stderr, "nl_addr_parse failed for %s: %s\n", ip_only, nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Nl_addr_parse failed", "%s: %s", ip_only, nl_geterror(nl_err));
		return 1;
	}
	nl_addr_set_prefixlen(local, (int)pref);

	struct rtnl_addr* raddr = rtnl_addr_alloc();
	if (!raddr) {
		fprintf(stderr, "rtnl_addr_alloc failed\n");
		// todo: free resources
		ERR(err, "Rtnl_addr_alloc failed");
		return 1;
	}
	rtnl_addr_set_family(raddr, AF_INET);
	rtnl_addr_set_scope(raddr, RT_SCOPE_UNIVERSE);
	rtnl_addr_set_local(raddr, local);
	rtnl_addr_set_ifindex(raddr, ifidx);

	if ((nl_err = rtnl_addr_add(sk, raddr, NLM_F_CREATE | NLM_F_ACK))) {
		fprintf(stderr, "failed to assign cidr to container's veth interface: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failed to assign cidr to container's veth interface", "%s", nl_geterror(nl_err));
		return 1;
	}

	if (setns(main_netns_fd, CLONE_NEWNET)) {
		fprintf(stderr, "failure re-associating thread to main net ns: %s\n", strerror(errno));
		// todo: free resources
		ERRF(err, "Failure re-associating thread to main net ns", "%s", strerror(errno));
		return 1;
	}

	return 0;
}

static int create_veth(Err* err, struct nl_sock* sk, int container_netns_fd, const char* container_veth_name,
		const char* container_veth_tmp_name, const char* host_veth_name, const char* container_veth_cidr) {
	int nl_err;

	// Create veth pair
	// We give container's veth a temporary name to ensure it does not conflict with host interfaces
	if (nl_err = rtnl_link_veth_add(sk, host_veth_name, container_veth_tmp_name, getpid())) {
		fprintf(stderr, "failure creating veth: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure creating veth", "%s", nl_geterror(nl_err));
		return 1;
	}

	int ifidx = if_nametoindex(container_veth_tmp_name);
	struct rtnl_link* peer_link;
	struct rtnl_link* changes_link;

	// fetches a reference (rtnl_link) to container's veth interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, ifidx, NULL, &peer_link)) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure filling rtnl_link information from kernel", "%s", nl_geterror(nl_err));
		return 1;
	}

	// create an rtnl_link to contain solely the desired change diff
	changes_link = rtnl_link_alloc();
	if (!changes_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		// todo: free resources
		ERR(err, "Failure allocating rtnl_link");
		return 1;
	}

	// set interface; set the new desired network namespace (container's)
	rtnl_link_set_ifindex(changes_link, ifidx);
	rtnl_link_set_ns_fd(changes_link, container_netns_fd);
	rtnl_link_set_name(changes_link, container_veth_name);

	// apply changes (to change container's veth network namespace)
	if (nl_err = rtnl_link_change(sk, peer_link, changes_link, 0)) {
		fprintf(stderr, "failure moving veth to container network namespace: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure moving veth to container network namespace", "%s", nl_geterror(nl_err));
		return 1;
	}

	if (configure_container_veth(err, container_netns_fd, container_veth_name, container_veth_cidr)) {
		fprintf(stderr, "failure configuring container's veth: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	rtnl_link_put(peer_link);
	rtnl_link_put(changes_link);
	return 0;
}

static int attach_host_veth_to_bridge(Err* err, struct nl_sock* sk, const char* host_veth_name) {
	int nl_err;
	struct rtnl_link* bridge_link;
	struct rtnl_link* veth_link;
	struct rtnl_link* changes_link;

	// fetches a reference (rtnl_link) to the bridge interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, 0, HOST_BRIDGE_NAME, &bridge_link)) {
		fprintf(stderr, "failure filling bridge information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure filling bridge information from kernel", "%s", nl_geterror(nl_err));
		return 1;
	}

	// fetches a reference (rtnl_link) to host's veth interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, 0, host_veth_name, &veth_link)) {
		fprintf(stderr, "failure filling host's veth information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure filling host's veth information from kernel", "%s", nl_geterror(nl_err));
		return 1;
	}

	// create an rtnl_link to contain solely the desired change diff
	changes_link = rtnl_link_alloc();
	if (!changes_link) {
		fprintf(stderr, "failure allocating rtnl_link\n");
		// todo: free resources
		ERR(err, "Failure allocating rtnl_link");
		return 1;
	}

	// fetches a reference (rtnl_link) to veth interface from kernel
	rtnl_link_set_ifindex(changes_link, if_nametoindex(host_veth_name));
	rtnl_link_set_master(changes_link, rtnl_link_get_ifindex(bridge_link));
	if (rtnl_link_change(sk, veth_link, changes_link, 0)) {
		fprintf(stderr, "failure enslaving host's veth to bridge: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure enslaving host's veth to bridge", "%s", nl_geterror(nl_err));
		return 1;
	}

	return 0;
}

int network_attach_container(Err* err, const char* container_netns_name, const char* container_netif_name,
		const char* container_netif_cidr, const char* container_id) {
	int nl_err;

	// Create netlink socket
	struct nl_sock* sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		// todo: free resources
		ERR(err, "Error allocating netlink socket");
		return 1;
	}
	if (nl_err = nl_connect(sk, NETLINK_ROUTE)) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Error creating/connecting to netlink socket", "%s", nl_geterror(nl_err));
		return 1;
	}

	// file descriptor for the target network namespace
	int container_netns_fd = open(container_netns_name, O_RDONLY | O_CLOEXEC);
	if (container_netns_fd < 0) {
		fprintf(stderr, "failure opening target net namespace fd: %s\n", strerror(errno));
		// todo: free resources
		ERRF(err, "Failure opening target net namespace fd", "%s", strerror(errno));
		return 1;
	}

	char host_if_name[16];
	char container_if_tmp_name[16];
	generate_deterministic_host_if_name(err, host_if_name, container_netns_name, container_netif_name, container_id);
	generate_deterministic_container_if_temporary_name(err, container_if_tmp_name, container_netns_name, container_netif_name, container_id);

	if (create_bridge(err, sk)) {
		fprintf(stderr, "failure creating bridge\n");
		// todo: free resources
		return 1;
	}

	if (create_veth(err, sk, container_netns_fd, container_netif_name, container_if_tmp_name, host_if_name, container_netif_cidr)) {
		fprintf(stderr, "failure creating veth\n");
		// todo: free resources
		return 1;
	}

	if (attach_host_veth_to_bridge(err, sk, host_if_name)) {
		fprintf(stderr, "failure attaching veth to bridge\n");
		// todo: free resources
		return 1;
	}

	close(container_netns_fd);
	nl_socket_free(sk);
	return 0;
}

int network_detach_container(Err* err, const char* container_netns_name, const char* container_netif_name, const char* container_id) {
	int nl_err;

	// Create netlink socket
	struct nl_sock* sk = nl_socket_alloc();
	if (!sk) {
		fprintf(stderr, "error allocating netlink socket\n");
		// todo: free resources
		ERR(err, "Error allocating netlink socket");
		return 1;
	}
	if (nl_err = nl_connect(sk, NETLINK_ROUTE)) { // NETLINK_ROUTE is one of netlink protocols; used for interfaces, routing, etc.
		fprintf(stderr, "error creating/connecting to netlink socket: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Error creating/connecting to netlink socket", "%s", nl_geterror(nl_err));
		return 1;
	}

	char host_veth_name[16];
	generate_deterministic_host_if_name(err, host_veth_name, container_netns_name, container_netif_name, container_id);

	// fetches a reference (rtnl_link) to host's veth interface from kernel
	struct rtnl_link* link;
	if (nl_err = rtnl_link_get_kernel(sk, 0, host_veth_name, &link)) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure filling rtnl_link information from kernel", "%s", nl_geterror(nl_err));
		return 1;
	}

	// delete the link (this also removes the peer end of the veth)
	if (nl_err = rtnl_link_delete(sk, link) < 0) {
		fprintf(stderr, "failure deleting veth pair %s: %s\n", host_veth_name, nl_geterror(nl_err));
		// todo: free resources
		ERRF(err, "Failure deleting veth pair", "%s: %s", host_veth_name, nl_geterror(nl_err));
		return 1;
	}

	rtnl_link_put(link);
	nl_socket_free(sk);
	return 0;
}
