#ifndef SKNF_ARGS_H
#define SKNF_ARGS_H

struct Args {
	const char* cni_version;
	const char* name;
	const char* type;
	const char* subnet;
	const char* cluster_cidr;
	const char* host_physical_interface;
	const char* cni_command;
	const char* cni_containerid;
	const char* cni_netns;
	const char* cni_ifname;
	const char* cni_path;
	const void* prev_result;

	void* json_input; // internal
};

int args_parse(struct Args* args);
void args_print(const struct Args* args);
void args_free(struct Args* args);

#endif