#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <stdint.h>

unsigned util_fnv1a32(const char *s) {
	const unsigned FNV_PRIME = 16777619u;
	unsigned h = 2166136261u;
	for (; *s; ++s) {
		h ^= (unsigned char)*s;
		h *= FNV_PRIME;
	}
	return h;
}

int util_cidr_parse(Err* err, const char* cidr, struct in_addr* addr, int* prefix) {
	char buffer[CIDR_BUFFER_LEN];
	strcpy(buffer, cidr);

	char* slash = strchr(buffer, '/');
	if (!slash) {
		fprintf(stderr, "util_cidr_parse: CIDR missing '/': %s\n", buffer);
		ERRF(err, "util_cidr_parse: CIDR missing '/'", "%s", buffer);
		return 1;
	}
	*prefix = atoi(slash + 1);
	*slash = '\0'; // remove IP prefix, keeping only IP address

	if (inet_pton(AF_INET, buffer, addr) != 1) {
		fprintf(stderr, "util_cidr_parse: invalid IPv4 '%s'\n", buffer);
		ERRF(err, "util_cidr_parse: invalid IPv4", "'%s'", buffer);
		return 1;
	}

	return 0;
}

int util_cidr_serialize(Err* err, struct in_addr addr, int prefix, char out[CIDR_BUFFER_LEN]) {
	char ip_only[INET_ADDRSTRLEN] = {0};
	if (!inet_ntop(AF_INET, &addr, ip_only, sizeof(ip_only))) {
		fprintf(stderr, "util_cidr_serialize: inet_ntop failed\n");
		ERR(err, "util_cidr_serialize: inet_ntop failed");
		return 1;
	}
	snprintf(out, (size_t)CIDR_BUFFER_LEN, "%s/%d", ip_only, prefix);
	return 0;
}