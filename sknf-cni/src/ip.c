#include "ip.h"
#include "io.h"

#include <stdio.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define IP_INFO_FILE_PATH "/tmp/sknf-cni-ips"

int ip_acquire(const char* cidr, char* buffer, int buf_size) {
    if (!cidr || !buffer || buf_size < INET_ADDRSTRLEN + 4) {
        fprintf(stderr, "ip_acquire: invalid args or small buffer\n");
        return -1;
    }

    size_t file_length = 0;

    if (io_file_exists(IP_INFO_FILE_PATH)) {
        // If the file already exists, then we get the last IP from the file

        int rc = io_read_file_into(IP_INFO_FILE_PATH, buffer, (size_t)buf_size, &file_length);
        if (rc != 0) {
            fprintf(stderr, "ip_acquire: failure reading '%s' (rc=%d)\n", IP_INFO_FILE_PATH, rc);
            return -1;
        }

        // Trim trailing whitespace and ensure NUL
        while (file_length > 0 &&
               (buffer[file_length-1] == '\n' ||
                buffer[file_length-1] == '\r' ||
                buffer[file_length-1] == ' '  ||
                buffer[file_length-1] == '\t')) {
            buffer[--file_length] = '\0';
        }
        buffer[file_length] = '\0';
    } else {
        // Otherwise, consider the CIDR parameter
        strcpy(buffer, cidr);
    }

    char* slash = strchr(buffer, '/');
    if (!slash) {
        fprintf(stderr, "ip_acquire: CIDR missing '/': %s\n", buffer);
        return -1;
    }
    int mask = atoi(slash + 1);
    *slash = '\0'; // remove IP prefix, keeping only IP address

    struct in_addr addr;
    if (inet_pton(AF_INET, buffer, &addr) != 1) {
        fprintf(stderr, "ip_acquire: invalid IPv4 '%s'\n", buffer);
        return -1;
    }

    uint32_t ip_int = ntohl(addr.s_addr);
    ip_int++; // no subnet checking yet
    addr.s_addr = htonl(ip_int);

    char ip_only[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &addr, ip_only, sizeof(ip_only))) {
        fprintf(stderr, "ip_acquire: inet_ntop failed\n");
        return -1;
    }

    // Recompose full CIDR into buffer
    snprintf(buffer, (size_t)buf_size, "%s/%d", ip_only, mask);

    // Persist the full CIDR
    if (io_write_text(IP_INFO_FILE_PATH, buffer) != 0) {
        fprintf(stderr, "ip_acquire: failed writing '%s'\n", buffer);
        return -1;
    }

    return 0;
}
