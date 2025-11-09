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

int ip_bridge(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]) {
	struct in_addr addr;
	int prefix;
	if (util_cidr_parse(err, cidr, &addr, &prefix)) {
		fprintf(stderr, "ip_bridge: unable to parse CIDR %s\n", cidr);
		return 1;
	}

	uint32_t ip_int = ntohl(addr.s_addr);
	// no subnet checking yet
	++ip_int; // get first IP
	addr.s_addr = htonl(ip_int);

	//if (util_cidr_serialize(err, addr, prefix, out)) {
	// temporary (maybe); ensure that CIDR prefix comprises whole cluster to make inter-node packets routable
	if (util_cidr_serialize(err, addr, 16, out)) {
		fprintf(stderr, "ip_bridge: unable to serialize CIDR\n");
		return 1;
	}
	return 0;
}

int ip_acquire(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]) {
	size_t file_length = 0;

	if (io_file_exists(IP_INFO_FILE_PATH)) {
		// If the file already exists, then we get the last IP from the file

		int rc = io_read_file_into(IP_INFO_FILE_PATH, out, CIDR_BUFFER_LEN, &file_length);
		if (rc != 0) {
			fprintf(stderr, "ip_acquire: failure reading '%s' (rc=%d)\n", IP_INFO_FILE_PATH, rc);
			ERRF(err, "Ip_acquire: failure reading file", "'%s' (rc=%d)", IP_INFO_FILE_PATH, rc);
			return 1;
		}

		// Trim trailing whitespace and ensure NUL
		while (file_length > 0 &&
			   (out[file_length-1] == '\n' ||
				out[file_length-1] == '\r' ||
				out[file_length-1] == ' '  ||
				out[file_length-1] == '\t')) {
			out[--file_length] = '\0';
		}
		out[file_length] = '\0';
	} else {
		// Otherwise, consider the CIDR parameter
		if (get_first_allocable_ip(err, cidr, out)) {
			fprintf(stderr, "ip_acquire: failure retrieving first allocable ip\n");
			return 1;
		}
	}

	struct in_addr addr;
	int prefix;
	if (util_cidr_parse(err, out, &addr, &prefix)) {
		fprintf(stderr, "ip_acquire: unable to parse CIDR %s\n", out);
		return 1;
	}

	uint32_t ip_int = ntohl(addr.s_addr);
	++ip_int; // no subnet checking yet
	addr.s_addr = htonl(ip_int);

	//if (util_cidr_serialize(err, addr, prefix, out)) {
	// temporary (maybe); ensure that CIDR prefix comprises whole cluster to make inter-node packets routable
	if (util_cidr_serialize(err, addr, 16, out)) {
		fprintf(stderr, "ip_acquire: unable to serialize CIDR\n");
		return 1;
	}

	// Persist the full CIDR
	if (io_write_text(IP_INFO_FILE_PATH, out) != 0) {
		fprintf(stderr, "ip_acquire: failed writing '%s'\n", out);
		ERRF(err, "Ip_acquire: failed writing", "'%s'", out);
		return 1;
	}

	return 0;
}
