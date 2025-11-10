#include "args.h"

#include <json-c/json.h>
#include <stdio.h>
#include <memory.h>
#include "def.h"

#define CNI_COMMAND_ENV_VAR_NAME "CNI_COMMAND"
#define CNI_CONTAINERID_ENV_VAR_NAME "CNI_CONTAINERID"
#define CNI_NETNS_ENV_VAR_NAME "CNI_NETNS"
#define CNI_IFNAME_ENV_VAR_NAME "CNI_IFNAME"
#define CNI_PATH_ENV_VAR_NAME "CNI_PATH"

#define CNI_VERSION_STDIN_JSON_KEY "cniVersion"
#define NAME_STDIN_JSON_KEY "name"
#define TYPE_STDIN_JSON_KEY "type"
#define SUBNET_STDIN_JSON_KEY "subnet"
#define CLUSTER_CIDR_STDIN_JSON_KEY "clusterCidr"
#define HOST_PHYSICAL_INTERFACE_STDIN_JSON_KEY "hostPhysicalInterface"
#define PREV_RESULT_STDIN_JSON_KEY "prevResult"

// TODO: dynamic buffer
#define INPUT_BUFFER_SIZE (64 * 1024)
char input_buffer[INPUT_BUFFER_SIZE];

static void mock_stdin_input() {
	FILE* f = fopen("conf/conf.json", "r");
	fread(input_buffer, INPUT_BUFFER_SIZE, 1, f);
	fclose(f);
}

static int args_validate_add_cmd(struct Args* args) {
	if (args->cni_version == NULL) {
		fprintf(stderr, "Failure: missing CNI version\n");
		return 1;
	}

	if (args->name == NULL) {
		fprintf(stderr, "Failure: missing name\n");
		return 1;
	}

	if (args->type == NULL) {
		fprintf(stderr, "Failure: missing type\n");
		return 1;
	}

	if (args->subnet == NULL) {
		fprintf(stderr, "Failure: missing subnet\n");
		return 1;
	}

	if (args->cluster_cidr == NULL) {
		fprintf(stderr, "Failure: missing cluster cidr\n");
		return 1;
	}

	if (args->host_physical_interface == NULL) {
		fprintf(stderr, "Failure: missing host physical interface\n");
		return 1;
	}

	if (args->cni_command == NULL) {
		fprintf(stderr, "Failure: missing CNI command\n");
		return 1;
	}

	if (args->cni_containerid == NULL) {
		fprintf(stderr, "Failure: missing CNI containerid\n");
		return 1;
	}

	if (args->cni_netns == NULL) {
		fprintf(stderr, "Failure: missing CNI netns\n");
		return 1;
	}

	if (args->cni_ifname == NULL) {
		fprintf(stderr, "Failure: missing CNI ifname\n");
		return 1;
	}

	if (args->cni_path == NULL) {
		fprintf(stderr, "Failure: missing CNI path\n");
		return 1;
	}

	return 0;
}

static int args_validate_del_cmd(struct Args* args) {
	return 0;
}

static int args_validate_check_cmd(struct Args* args) {
	return 0;
}

int args_parse(struct Args* args) {
	memset(args, 0, sizeof(struct Args));

	size_t n = fread(input_buffer, 1, sizeof(input_buffer) - 1, stdin);
	input_buffer[n] = '\0';

	//mock_stdin_input();
	//fprintf(stderr, "%s\n", input_buffer);

	args->json_input = json_tokener_parse(input_buffer);

	struct json_object* cni_version_obj;
	struct json_object* name_obj;
	struct json_object* type_obj;
	struct json_object* subnet_obj;
	struct json_object* cluster_cidr_obj;
	struct json_object* host_physical_interface_obj;
	struct json_object* prev_result_obj;

	if (json_object_object_get_ex(args->json_input, CNI_VERSION_STDIN_JSON_KEY, &cni_version_obj)) {
		args->cni_version = json_object_get_string(cni_version_obj);
	}

	if (json_object_object_get_ex(args->json_input, NAME_STDIN_JSON_KEY, &name_obj)) {
		args->name = json_object_get_string(name_obj);
	}

	if (json_object_object_get_ex(args->json_input, TYPE_STDIN_JSON_KEY, &type_obj)) {
		args->type = json_object_get_string(type_obj);
	}

	if (json_object_object_get_ex(args->json_input, SUBNET_STDIN_JSON_KEY, &subnet_obj)) {
		args->subnet = json_object_get_string(subnet_obj);
	}

	if (json_object_object_get_ex(args->json_input, CLUSTER_CIDR_STDIN_JSON_KEY, &cluster_cidr_obj)) {
		args->cluster_cidr = json_object_get_string(cluster_cidr_obj);
	}

	if (json_object_object_get_ex(args->json_input, HOST_PHYSICAL_INTERFACE_STDIN_JSON_KEY, &host_physical_interface_obj)) {
		args->host_physical_interface = json_object_get_string(host_physical_interface_obj);
	}

	if (json_object_object_get_ex(args->json_input, PREV_RESULT_STDIN_JSON_KEY, &prev_result_obj)) {
		args->prev_result = prev_result_obj;
	}

	args->cni_command = getenv(CNI_COMMAND_ENV_VAR_NAME);
	args->cni_containerid = getenv(CNI_CONTAINERID_ENV_VAR_NAME);
	args->cni_netns = getenv(CNI_NETNS_ENV_VAR_NAME);
	args->cni_ifname = getenv(CNI_IFNAME_ENV_VAR_NAME);
	args->cni_path = getenv(CNI_PATH_ENV_VAR_NAME);

	if (args->cni_command == NULL) {
		fprintf(stderr, "Failure: missing CNI command\n");
		args_free(args);
		return 1;
	}

	if (strcmp(args->cni_version, CNI_VERSION)) {
		fprintf(stderr, "Falure: unsupported CNI version, requires %s, received %s\n", CNI_VERSION, args->cni_version);
		args_free(args);
		return 1;
	}

	if (!strcmp(args->cni_command, CNI_CMD_ADD)) {
		if (args_validate_add_cmd(args)) {
			args_free(args);
			return 1;
		}
	} else if (!strcmp(args->cni_command, CNI_CMD_DEL)) {
		if (args_validate_del_cmd(args)) {
			args_free(args);
			return 1;
		}
	} else if (!strcmp(args->cni_command, CNI_CMD_CHECK)) {
		if (args_validate_check_cmd(args)) {
			args_free(args);
			return 1;
		}
	}

	return 0;
}

void args_print(const struct Args* args) {
	fprintf(stderr, "cni version is %s\n", args->cni_version);
	fprintf(stderr, "name is %s\n", args->name);
	fprintf(stderr, "type is %s\n", args->type);
	fprintf(stderr, "cni_command is %s\n", args->cni_command);
	fprintf(stderr, "cni_containerid is %s\n", args->cni_containerid);
	fprintf(stderr, "cni_netns is %s\n", args->cni_netns);
	fprintf(stderr, "cni_ifname is %s\n", args->cni_ifname);
	fprintf(stderr, "cni_path is %s\n", args->cni_path);
}

void args_free(struct Args* args) {
	if (args->json_input) {
		json_object_put(args->json_input);
		args->json_input = NULL;
	}
}