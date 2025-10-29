#include "cmd.h"

#include <json-c/json.h>
#include <stdio.h>
#include "def.h"
#include "ip.h"
#include "network.h"

static void emit_add_response(const char* address) {
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

int cmd_add(const struct Args* args) {
	if (network_setup(args->cni_netns, args->cni_ifname)) {
		fprintf(stderr, "failure setting up network\n");
		return 1;
	}

	char ip_address[256];
	if (ip_acquire(args->subnet, ip_address, 256)) {
		fprintf(stderr, "failure acquiring an IP address\n");
		return 1;
	}
	emit_add_response(ip_address);
    return 0;
}