#include "cmd.h"

#include <json-c/json.h>
#include <stdio.h>
#include "def.h"
#include "ip.h"
#include "network.h"

static void emit_add_response(const struct Args* args, const char* container_netif_cidr) {
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
	json_object_object_add(ip_obj, "address", json_object_new_string(container_netif_cidr));
	json_object_object_add(ip_obj, "interface", json_object_new_int(0));
	json_object_array_add(ips_arr, ip_obj);
	json_object_object_add(json_response_obj, "ips", ips_arr);

    if (args->prev_result != NULL) {
        json_object_object_add(json_response_obj, "prevResult", args->prev_result);
    }

	fprintf(stderr, "emit_response: emitting response: %s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	printf("%s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	json_object_put(json_response_obj);
}

static void emit_version_response(const struct Args* args) {
	struct json_object* json_response_obj = json_object_new_object();

	json_object_object_add(json_response_obj, "cniVersion", json_object_new_string(CNI_VERSION));
	struct json_object* supported_versions_array = json_object_new_array();
	json_object_array_add(supported_versions_array, json_object_new_string(CNI_VERSION));
	json_object_object_add(json_response_obj, "supportedVersions", supported_versions_array);

	fprintf(stderr, "emit_response: emitting response: %s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	printf("%s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	json_object_put(json_response_obj);
}

static void emit_error_response(Err err) {
	struct json_object* json_response_obj = json_object_new_object();

	if (!err.initialized) {
		ERR(&err, "An unknown error happened");
	}

	json_object_object_add(json_response_obj, "cniVersion", json_object_new_string(CNI_VERSION));
	json_object_object_add(json_response_obj, "code", json_object_new_int(err.code));
	json_object_object_add(json_response_obj, "msg", json_object_new_string(err.msg));
	json_object_object_add(json_response_obj, "details", json_object_new_string(err.details));

	fprintf(stderr, "emit_response: emitting response: %s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	printf("%s\n", json_object_to_json_string_ext(json_response_obj, JSON_C_TO_STRING_PLAIN));
	json_object_put(json_response_obj);
}

int cmd_add(const struct Args* args) {
    // TODO: Return error if interface already exists in container

    // TODO: set IP to interface
    // TODO: set routes (probably not needed for L2 communication)

	Err err;
	ERR_INIT(&err);

	char container_netif_cidr[256];
	if (ip_acquire(&err, args->subnet, container_netif_cidr, 256)) {
		fprintf(stderr, "failure acquiring an IP address\n");
		emit_error_response(err);
		return 1;
	}

	if (network_attach_container(&err, args->cni_netns, args->cni_ifname, container_netif_cidr, args->cni_containerid)) {
		fprintf(stderr, "failure attaching container network\n");
		emit_error_response(err);
		return 1;
	}

	emit_add_response(args, container_netif_cidr);
    return 0;
}

int cmd_del(const struct Args* args) {
	Err err;
	ERR_INIT(&err);

    if (network_detach_container(&err, args->cni_netns, args->cni_ifname, args->cni_containerid)) {
		fprintf(stderr, "failure detaching container network\n");
		emit_error_response(err);
		return 1;
    }

    return 0;
}

int cmd_status(const struct Args* args) {
	return 0;
}

int cmd_check(const struct Args* args) {
	return 0;
}

int cmd_version(const struct Args* args) {
	emit_version_response(args);
	return 0;
}

int cmd_gc(const struct Args* args) {
	return 0;
}