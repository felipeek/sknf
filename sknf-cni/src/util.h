#ifndef SKNF_UTIL_H
#define SKNF_UTIL_H

#include "def.h"
#include "err.h"

#include <arpa/inet.h>

unsigned util_fnv1a32(const char *s);
int util_cidr_parse(Err* err, const char* cidr, struct in_addr* addr, int* prefix);
int util_cidr_serialize(Err* err, struct in_addr addr, int prefix, char out[CIDR_BUFFER_LEN]);

#endif