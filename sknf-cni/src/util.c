#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

unsigned util_fnv1a32(const char *s) {
	const unsigned FNV_PRIME = 16777619u;
	unsigned h = 2166136261u;
	for (; *s; ++s) {
		h ^= (unsigned char)*s;
		h *= FNV_PRIME;
	}
	return h;
}
