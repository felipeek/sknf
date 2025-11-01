#ifndef SKNF_IP_H
#define SKNF_IP_H

#include "err.h"

int ip_acquire(Err* err, const char* cidr, char* buffer, int buf_size);

#endif