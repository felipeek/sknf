#include "sys.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

static int write_sysctl(Err* err, const char *path, const char *value) {
	FILE *f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
        ERR(err, "failed to open %s: %s", path, strerror(errno));
		return 1;
	}

	if (fputs(value, f) == EOF) {
		fprintf(stderr, "failed to write to %s: %s\n", path, strerror(errno));
        ERR(err, "failed to write to %s: %s", path, strerror(errno));
		fclose(f);
		return 1;
	}

	fclose(f);
	return 0;
}

int sys_enable_br_netfilter(Err* err) {
	/* Try to load the module. If it fails, assume br_netfilter is built-in in kernel. */
	int rc = system("modprobe br_netfilter 2>/dev/null");
	if (rc != 0) {
		//fprintf(stderr, "modprobe br_netfilter failed (rc=%d), continuing\n", rc);
	}

	if (write_sysctl(err, "/proc/sys/net/bridge/bridge-nf-call-iptables", "1\n") != 0) {
		fprintf(stderr, "could not enable bridge-nf-call-iptables\n");
        return 1;
	}

	if (write_sysctl(err, "/proc/sys/net/bridge/bridge-nf-call-ip6tables", "1\n") != 0) {
		fprintf(stderr, "could not enable bridge-nf-call-ip6tables\n");
        return 1;
	}

    return 0;
}
