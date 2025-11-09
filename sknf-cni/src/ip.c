#include "ip.h"
#include "io.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define IP_INFO_FILE_PATH "/tmp/sknf-cni-ips"

static int get_first_allocable_ip(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]) {
    char* slash = strchr(cidr, '/');
    if (!slash) {
        fprintf(stderr, "get_first_allocable_ip: CIDR missing '/': %s\n", cidr);
        ERRF(err, "get_first_allocable_ip: CIDR missing '/'", "%s", cidr);
        return 1;
    }
    int mask = atoi(slash + 1);
    *slash = '\0'; // remove IP prefix, keeping only IP address

    struct in_addr addr;
    if (inet_pton(AF_INET, cidr, &addr) != 1) {
        fprintf(stderr, "get_first_allocable_ip: invalid IPv4 '%s'\n", out);
        ERRF(err, "get_first_allocable_ip: invalid IPv4", "'%s'", out);
        return 1;
    }

    uint32_t ip_int = ntohl(addr.s_addr);
    // no subnet checking yet
    ++ip_int; // skip one IP (reserved for bridge)
    addr.s_addr = htonl(ip_int);

    char ip_only[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &addr, ip_only, sizeof(ip_only))) {
        fprintf(stderr, "get_first_allocable_ip: inet_ntop failed\n");
        ERR(err, "get_first_allocable_ip: inet_ntop failed");
        return 1;
    }

    // Recompose full CIDR into out
    snprintf(out, (size_t)CIDR_BUFFER_LEN, "%s/%d", ip_only, mask);
    return 0;
}

int ip_bridge(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]) {
    char buffer[CIDR_BUFFER_LEN];
    strcpy(buffer, cidr);

    char* slash = strchr(buffer, '/');
    if (!slash) {
        fprintf(stderr, "ip_bridge: CIDR missing '/': %s\n", buffer);
        ERRF(err, "ip_bridge: CIDR missing '/'", "%s", buffer);
        return 1;
    }
    int mask = atoi(slash + 1);
    *slash = '\0'; // remove IP prefix, keeping only IP address

    struct in_addr addr;
    if (inet_pton(AF_INET, buffer, &addr) != 1) {
        fprintf(stderr, "ip_bridge: invalid IPv4 '%s'\n", out);
        ERRF(err, "ip_bridge: invalid IPv4", "'%s'", out);
        return 1;
    }

    uint32_t ip_int = ntohl(addr.s_addr);
    // no subnet checking yet
    ++ip_int; // get first IP
    addr.s_addr = htonl(ip_int);

    char ip_only[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &addr, ip_only, sizeof(ip_only))) {
        fprintf(stderr, "ip_bridge: inet_ntop failed\n");
        ERR(err, "ip_bridge: inet_ntop failed");
        return 1;
    }

    // Recompose full CIDR into out
    //snprintf(out, (size_t)CIDR_BUFFER_LEN, "%s/%d", ip_only, mask);

    // temporary (maybe); ensure that CIDR prefix comprises whole cluster to make inter-node packets routable
    snprintf(out, (size_t)CIDR_BUFFER_LEN, "%s/%d", ip_only, 16);
    return 0;
}

int ip_acquire(Err* err, const char* cidr, char out[CIDR_BUFFER_LEN]) {
    size_t file_length = 0;

    if (io_file_exists(IP_INFO_FILE_PATH)) {
        // If the file already exists, then we get the last IP from the file

        int rc = io_read_file_into(IP_INFO_FILE_PATH, out, CIDR_BUFFER_LEN, &file_length);
        if (rc != 0) {
            fprintf(stderr, "ip_acquire: failure reading '%s' (rc=%d)\n", IP_INFO_FILE_PATH, rc);
            ERRF(err, "Ip_acquire: failure reading file", "'%s' (rc=%d)", IP_INFO_FILE_PATH, rc);
            return 1;
        }

        // Trim trailing whitespace and ensure NUL
        while (file_length > 0 &&
               (out[file_length-1] == '\n' ||
                out[file_length-1] == '\r' ||
                out[file_length-1] == ' '  ||
                out[file_length-1] == '\t')) {
            out[--file_length] = '\0';
        }
        out[file_length] = '\0';
    } else {
        // Otherwise, consider the CIDR parameter
        if (get_first_allocable_ip(err, cidr, out)) {
            fprintf(stderr, "ip_acquire: failure retrieving first allocable ip\n");
            return 1;
        }
    }

    char* slash = strchr(out, '/');
    if (!slash) {
        fprintf(stderr, "ip_acquire: CIDR missing '/': %s\n", out);
        ERRF(err, "Ip_acquire: CIDR missing '/'", "%s", out);
        return 1;
    }
    int mask = atoi(slash + 1);
    *slash = '\0'; // remove IP prefix, keeping only IP address

    struct in_addr addr;
    if (inet_pton(AF_INET, out, &addr) != 1) {
        fprintf(stderr, "ip_acquire: invalid IPv4 '%s'\n", out);
        ERRF(err, "Ip_acquire: invalid IPv4", "'%s'", out);
        return 1;
    }

    uint32_t ip_int = ntohl(addr.s_addr);
    ++ip_int; // no subnet checking yet
    addr.s_addr = htonl(ip_int);

    char ip_only[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &addr, ip_only, sizeof(ip_only))) {
        fprintf(stderr, "ip_acquire: inet_ntop failed\n");
        ERR(err, "Ip_acquire: inet_ntop failed");
        return 1;
    }

    // Recompose full CIDR into out
    //snprintf(out, (size_t)CIDR_BUFFER_LEN, "%s/%d", ip_only, mask);

    // temporary (maybe); ensure that CIDR prefix comprises whole cluster to make inter-node packets routable
    snprintf(out, (size_t)CIDR_BUFFER_LEN, "%s/%d", ip_only, 16);

    // Persist the full CIDR
    if (io_write_text(IP_INFO_FILE_PATH, out) != 0) {
        fprintf(stderr, "ip_acquire: failed writing '%s'\n", out);
        ERRF(err, "Ip_acquire: failed writing", "'%s'", out);
        return 1;
    }

    return 0;
}
