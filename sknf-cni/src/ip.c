#include "ip.h"
#include "io.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"

#define IP_INFO_FILE_PATH "/tmp/sknf-cni-ips"

static int get_first_allocable_ip(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]) {
	struct in_addr addr;
	int prefix;
	if (util_cidr_parse(err, cidr, &addr, &prefix)) {
		fprintf(stderr, "get_first_allocable_ip: unable to parse CIDR %s\n", cidr);
		return 1;
	}

	uint32_t ip_int = ntohl(addr.s_addr);
	// no subnet checking yet
	++ip_int; // skip one IP (reserved for bridge)
	addr.s_addr = htonl(ip_int);

	if (util_cidr_serialize(err, addr, prefix, out)) {
		fprintf(stderr, "get_first_allocable_ip: unable to serialize CIDR\n");
		return 1;
	}
	return 0;
}

int ip_bridge(Err* err, const char* node_cidr, const char* cluster_cidr, char out[CIDR_BUFFER_LEN]) {
	// Parse node CIDR
	struct in_addr node_cidr_addr;
	int node_cidr_prefix;
	if (util_cidr_parse(err, node_cidr, &node_cidr_addr, &node_cidr_prefix)) {
		fprintf(stderr, "ip_bridge: unable to parse node CIDR %s\n", node_cidr);
		return 1;
	}

	// Parse cluster CIDR
	struct in_addr cluster_cidr_addr;
	int cluster_cidr_prefix;
	if (util_cidr_parse(err, cluster_cidr, &cluster_cidr_addr, &cluster_cidr_prefix)) {
		fprintf(stderr, "ip_bridge: unable to parse cluster CIDR %s\n", cluster_cidr);
		return 1;
	}

	// Increment node CIDR one unit (we assign the first IP to the bridge)
	uint32_t ip_int = ntohl(node_cidr_addr.s_addr);
	// no subnet checking yet
	++ip_int; // get first IP
	node_cidr_addr.s_addr = htonl(ip_int);

	// serialize as <bridge-IP>/<clusterWideCidrPrefix> for completeness, as the virtual L2 domain comprises the whole cluster
	if (util_cidr_serialize(err, node_cidr_addr, cluster_cidr_prefix, out)) {
		fprintf(stderr, "ip_bridge: unable to serialize CIDR\n");
		return 1;
	}
	return 0;
}

// TODO: This function will not work properly if CNI plugin is invoked in multiple times in parallel, because we
// don't lock on the file.
int ip_container_acquire(Err* err, const char* node_cidr, const char* cluster_cidr, char out[CIDR_BUFFER_LEN]) {
	size_t file_length = 0;
	char last_acquired_cidr[CIDR_BUFFER_LEN];

	if (io_file_exists(IP_INFO_FILE_PATH)) {
		// If the file already exists, then we get the last IP from the file
		int rc = io_read_file_into(IP_INFO_FILE_PATH, last_acquired_cidr, CIDR_BUFFER_LEN, &file_length);
		if (rc != 0) {
			fprintf(stderr, "ip_container_acquire: failure reading '%s' (rc=%d)\n", IP_INFO_FILE_PATH, rc);
			ERRF(err, "ip_container_acquire: failure reading file", "'%s' (rc=%d)", IP_INFO_FILE_PATH, rc);
			return 1;
		}

		// Trim trailing whitespace and ensure NUL
		while (file_length > 0 &&
			(last_acquired_cidr[file_length-1] == '\n' ||
				last_acquired_cidr[file_length-1] == '\r' ||
				last_acquired_cidr[file_length-1] == ' '  ||
				last_acquired_cidr[file_length-1] == '\t')) {
			last_acquired_cidr[--file_length] = '\0';
		}
		last_acquired_cidr[file_length] = '\0';
	} else {
		// Otherwise, consider the CIDR parameter
		if (get_first_allocable_ip(err, node_cidr, last_acquired_cidr)) {
			fprintf(stderr, "ip_container_acquire: failure retrieving first allocable ip\n");
			return 1;
		}
	}

	// Parse last acquired CIDR
	struct in_addr last_acquired_cidr_addr;
	int last_acquired_cidr_prefix;
	if (util_cidr_parse(err, last_acquired_cidr, &last_acquired_cidr_addr, &last_acquired_cidr_prefix)) {
		fprintf(stderr, "ip_container_acquire: unable to parse last acquired CIDR %s\n", last_acquired_cidr);
		return 1;
	}

	// Parse cluster CIDR
	struct in_addr cluster_cidr_addr;
	int cluster_cidr_prefix;
	if (util_cidr_parse(err, cluster_cidr, &cluster_cidr_addr, &cluster_cidr_prefix)) {
		fprintf(stderr, "ip_bridge: unable to parse cluster CIDR %s\n", cluster_cidr);
		return 1;
	}

	uint32_t ip_int = ntohl(last_acquired_cidr_addr.s_addr);
	++ip_int; // no subnet checking yet
	last_acquired_cidr_addr.s_addr = htonl(ip_int);

	// serialize as <bridge-IP>/<clusterWideCidrPrefix> because the virtual L2 domain comprises the whole cluster
	// this is necessary to ensure that the container will consider other containers/pods that are living in other nodes
	// to be on-link in its L2 domain, thus dispatching these frames on-link
	if (util_cidr_serialize(err, last_acquired_cidr_addr, cluster_cidr_prefix, out)) {
		fprintf(stderr, "ip_container_acquire: unable to serialize node CIDR\n");
		return 1;
	}

	// Persist the full CIDR
	// TODO: This is not bulletproof, if multiple CNI plugin invokations change the file simultaneously bad things will happen
	if (io_write_text(IP_INFO_FILE_PATH, out) != 0) {
		fprintf(stderr, "ip_container_acquire: failed writing '%s'\n", out);
		ERRF(err, "ip_container_acquire: failed writing", "'%s'", out);
		return 1;
	}

	return 0;
}
