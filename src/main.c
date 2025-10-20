#include <stdio.h>
#include <stdlib.h>
#include <json-c/json.h>

#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <errno.h>

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/route/link.h>
#include <netlink/route/link/veth.h>

#define CNI_COMMAND_ENV_VAR_NAME "CNI_COMMAND"
#define CNI_CONTAINERID_ENV_VAR_NAME "CNI_CONTAINERID"
#define CNI_NETNS_ENV_VAR_NAME "CNI_NETNS"
#define CNI_IFNAME_ENV_VAR_NAME "CNI_IFNAME"
#define CNI_PATH_ENV_VAR_NAME "CNI_PATH"

#define CNI_VERSION_STDIN_JSON_KEY "cniVersion"
#define NAME_STDIN_JSON_KEY "name"
#define TYPE_STDIN_JSON_KEY "type"

// TODO: dynamic buffer
#define INPUT_BUFFER_SIZE (64 * 1024)
char input_buffer[INPUT_BUFFER_SIZE];

static void mock_stdin_input() {
	FILE* f = fopen("conf/conf.json", "r");
	fread(input_buffer, INPUT_BUFFER_SIZE, 1, f);
	fclose(f);
}

int main() {
	size_t n = fread(input_buffer, 1, sizeof(input_buffer) - 1, stdin);
	input_buffer[n] = '\0';

	//mock_stdin_input();
	//fprintf(stderr, "%s\n", input_buffer);

	struct json_object* parsed_input = json_tokener_parse(input_buffer);

	struct json_object* cni_version_obj;
	struct json_object* name_obj;
	struct json_object* type_obj;

	const char* cni_version = NULL;
	const char* name = NULL;
	const char* type = NULL;
	
	if (!json_object_object_get_ex(parsed_input, CNI_VERSION_STDIN_JSON_KEY, &cni_version_obj)) {
		fprintf(stderr, "missing cni_version\n");
		// todo: free resources
		return 1;
	}

	if (!json_object_object_get_ex(parsed_input, NAME_STDIN_JSON_KEY, &name_obj)) {
		fprintf(stderr, "missing name\n");
		// todo: free resources
		return 1;
	}

	if (!json_object_object_get_ex(parsed_input, TYPE_STDIN_JSON_KEY, &type_obj)) {
		fprintf(stderr, "missing type\n");
		// todo: free resources
		return 1;
	}

	cni_version = json_object_get_string(cni_version_obj);
	name = json_object_get_string(name_obj);
	type = json_object_get_string(type_obj);

	//fprintf(stderr, "cni version is %s\n", cni_version);
	//fprintf(stderr, "name is %s\n", name);
	//fprintf(stderr, "type is %s\n", type);

	const char* cni_command = getenv(CNI_COMMAND_ENV_VAR_NAME);
	const char* cni_containerid = getenv(CNI_CONTAINERID_ENV_VAR_NAME);
	const char* cni_netns = getenv(CNI_NETNS_ENV_VAR_NAME);
	const char* cni_ifname = getenv(CNI_IFNAME_ENV_VAR_NAME);
	const char* cni_path = getenv(CNI_PATH_ENV_VAR_NAME);

	fprintf(stderr, "cni_command is %s\n", cni_command);
	fprintf(stderr, "cni_containerid is %s\n", cni_containerid);
	fprintf(stderr, "cni_netns is %s\n", cni_netns);
	fprintf(stderr, "cni_ifname is %s\n", cni_ifname);
	fprintf(stderr, "cni_path is %s\n", cni_path);

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

	// Create veth pair
	if (nl_err = rtnl_link_veth_add(sk, "veth-host", "veth-ctn", getpid())) {
		fprintf(stderr, "failure creating veth: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	int ifidx = if_nametoindex("veth-ctn");
	struct rtnl_link* peer_link;
	struct rtnl_link* changes_link;

	// fetches a reference (rtnl_link) to the veth-ctn interface from kernel
	if (nl_err = rtnl_link_get_kernel(sk, ifidx, NULL, &peer_link)) {
		fprintf(stderr, "failure filling rtnl_link information from kernel: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	// file descriptor for the target network namespace
	int nsfd = open(cni_netns, O_RDONLY | O_CLOEXEC);
	if (nsfd < 0) {
		fprintf(stderr, "failure opening target net namespace fd: %s\n", strerror(errno));
		// todo: free resources
		return 1;
	}

	// create an rtnl_link to contain solely the desired change diff
	changes_link = rtnl_link_alloc();
	if (!changes_link) {
		fprintf(stderr, "failure allocating rtnl_link: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	// set interface; set the new desired network namespace (container's)
	rtnl_link_set_ifindex(changes_link, ifidx);
	rtnl_link_set_ns_fd(changes_link, nsfd);

	// apply changes (to change veth-ctn's network namespace)
	if (nl_err = rtnl_link_change(sk, peer_link, changes_link, 0)) {
		fprintf(stderr, "failure moving veth to container network namespace: %s\n", nl_geterror(nl_err));
		// todo: free resources
		return 1;
	}

	close(nsfd);
	rtnl_link_put(peer_link);
	rtnl_link_put(changes_link);
	nl_socket_free(sk);
	json_object_put(parsed_input);

	return 0;
}