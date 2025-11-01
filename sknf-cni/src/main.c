#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "def.h"
#include "args.h"
#include "cmd.h"

int main() {
	struct Args args;
	if (args_parse(&args)) {
		fprintf(stderr, "Failure parsing arguments\n");
		return 1;
	}

	fprintf(stderr, "Starting...\n");
	fflush(stderr);

	srand(time(NULL));

	if (!strcmp(args.cni_command, CNI_CMD_ADD)) {
		return cmd_add(&args);
	} else if (!strcmp(args.cni_command, CNI_CMD_DEL)) {
		return cmd_del(&args);
	} else if (!strcmp(args.cni_command, CNI_CMD_STATUS)) {
		return cmd_status(&args);
	} else if (!strcmp(args.cni_command, CNI_CMD_VERSION)) {
		return cmd_version(&args);
	} else if (!strcmp(args.cni_command, CNI_CMD_CHECK)) {
		return cmd_check(&args);
	} else if (!strcmp(args.cni_command, CNI_CMD_GC)) {
		return cmd_gc(&args);
	} else {
		fprintf(stderr, "failure: received unknown command %s\n", args.cni_command);
		args_free(&args);
		return 1;
	}
}