#ifndef SKNF_IP_H
#define SKNF_IP_H

#include "err.h"

#define CIDR_BUFFER_LEN 64

int ip_bridge(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]);
int ip_acquire(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]);

#endif