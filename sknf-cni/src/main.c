#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>

#include "network.h"
#include "ip.h"

#define CNI_COMMAND_ENV_VAR_NAME "CNI_COMMAND"
#define CNI_CONTAINERID_ENV_VAR_NAME "CNI_CONTAINERID"
#define CNI_NETNS_ENV_VAR_NAME "CNI_NETNS"
#define CNI_IFNAME_ENV_VAR_NAME "CNI_IFNAME"
#define CNI_PATH_ENV_VAR_NAME "CNI_PATH"

#define CNI_VERSION_STDIN_JSON_KEY "cniVersion"
#define NAME_STDIN_JSON_KEY "name"
#define TYPE_STDIN_JSON_KEY "type"
#define SUBNET_STDIN_JSON_KEY "subnet"

#define CNI_VERSION "0.4.0"

// TODO: dynamic buffer
#define INPUT_BUFFER_SIZE (64 * 1024)
char input_buffer[INPUT_BUFFER_SIZE];

static void mock_stdin_input() {
	FILE* f = fopen("conf/conf.json", "r");
	fread(input_buffer, INPUT_BUFFER_SIZE, 1, f);
	fclose(f);
}

static void emit_response(const char* address) {
	struct json_object* json_response_obj = json_object_new_object();

	json_object_object_add(json_response_obj, "cniVersion", json_object_new_string(CNI_VERSION));

	// interfaces array
	struct json_object* interfaces_arr = json_object_new_array();
	struct json_object* iface_obj = json_object_new_object();
	json_object_object_add(iface_obj, "name", json_object_new_string("eth0"));
	json_object_array_add(interfaces_arr, iface_obj);
	json_object_object_add(json_response_obj, "interfaces", interfaces_arr);

	// ips array
	struct json_object* ips_arr = json_object_new_array();
	struct json_object* ip_obj = json_object_new_object();
	json_object_object_add(ip_obj, "version", json_object_new_string("4"));
	json_object_object_add(ip_obj, "address", json_object_new_string(address));
	json_object_object_add(ip_obj, "interface", json_object_new_int(0));
	json_object_array_add(ips_arr, ip_obj);
	json_object_object_add(json_response_obj, "ips", ips_arr);

	fprintf(stderr, "emit_response: emitting response: %s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	printf("%s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	json_object_put(json_response_obj);
}

int main() {
	size_t n = fread(input_buffer, 1, sizeof(input_buffer) - 1, stdin);
	input_buffer[n] = '\0';

	//mock_stdin_input();
	//fprintf(stderr, "%s\n", input_buffer);

	struct json_object* parsed_input = json_tokener_parse(input_buffer);

	struct json_object* cni_version_obj;
	struct json_object* name_obj;
	struct json_object* type_obj;
	struct json_object* subnet_obj;

	const char* cni_version = NULL;
	const char* name = NULL;
	const char* type = NULL;
	const char* subnet = NULL;
	
	if (!json_object_object_get_ex(parsed_input, CNI_VERSION_STDIN_JSON_KEY, &cni_version_obj)) {
		fprintf(stderr, "missing cni_version\n");
		// todo: free resources
		return 1;
	}

	if (!json_object_object_get_ex(parsed_input, NAME_STDIN_JSON_KEY, &name_obj)) {
		fprintf(stderr, "missing name\n");
		// todo: free resources
		return 1;
	}

	if (!json_object_object_get_ex(parsed_input, TYPE_STDIN_JSON_KEY, &type_obj)) {
		fprintf(stderr, "missing type\n");
		// todo: free resources
		return 1;
	}

	if (!json_object_object_get_ex(parsed_input, SUBNET_STDIN_JSON_KEY, &subnet_obj)) {
		fprintf(stderr, "missing subnet\n");
		// todo: free resources
		return 1;
	}

	cni_version = json_object_get_string(cni_version_obj);
	name = json_object_get_string(name_obj);
	type = json_object_get_string(type_obj);
	subnet = json_object_get_string(subnet_obj);

	//fprintf(stderr, "cni version is %s\n", cni_version);
	//fprintf(stderr, "name is %s\n", name);
	//fprintf(stderr, "type is %s\n", type);

	const char* cni_command = getenv(CNI_COMMAND_ENV_VAR_NAME);
	const char* cni_containerid = getenv(CNI_CONTAINERID_ENV_VAR_NAME);
	const char* cni_netns = getenv(CNI_NETNS_ENV_VAR_NAME);
	const char* cni_ifname = getenv(CNI_IFNAME_ENV_VAR_NAME);
	const char* cni_path = getenv(CNI_PATH_ENV_VAR_NAME);

	fprintf(stderr, "cni_command is %s\n", cni_command);
	fprintf(stderr, "cni_containerid is %s\n", cni_containerid);
	fprintf(stderr, "cni_netns is %s\n", cni_netns);
	fprintf(stderr, "cni_ifname is %s\n", cni_ifname);
	fprintf(stderr, "cni_path is %s\n", cni_path);

	if (strcmp(cni_version, CNI_VERSION)) {
		fprintf(stderr, "unsupported CNI version, requires %s, received %s\n", CNI_VERSION, cni_version);
		return 1;
	}

	if (network_setup(cni_netns, cni_ifname)) {
		fprintf(stderr, "failure setting up network\n");
		return 1;
	}

	char ip_address[256];
	if (ip_acquire(subnet, ip_address, 256)) {
		fprintf(stderr, "failure acquiring an IP address\n");
		return 1;
	}
	emit_response(ip_address);

	json_object_put(parsed_input);

	return 0;
}