#ifndef SKNF_IP_H
#define SKNF_IP_H

#include "def.h"
#include "err.h"

int ip_bridge(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]);
int ip_acquire(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]);

#endif