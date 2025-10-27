#include "io.h"

#include <stdio.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int io_file_exists(const char *path) {
    if (!path) {
        return 0;
    }

    FILE *file = fopen(path, "r");
    if (file) {
        fclose(file);
        return 1;
    }
    return 0;
}

int io_read_file_into(const char *path, char *buf, size_t bufsize, size_t *out_len) {
	if (!path || !buf || bufsize == 0) {
		fprintf(stderr, "io_read_file_into: invalid arguments\n");
		return -1;
	}

	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "io_read_file_into: fopen %s", strerror(errno));
		return -1;
	}

	size_t total = 0;
	int rc = 0; /* 0 = ok, 1 = truncated */

	/* Reserve 1 byte for '\0' */
	size_t cap = (bufsize > 0) ? bufsize - 1 : 0;

	while (total < cap) {
		size_t n = fread(buf + total, 1, cap - total, f);
		total += n;

		if (n == 0) {
			if (feof(f)) break;
			if (ferror(f)) {
                fprintf(stderr, "io_read_file_into: fread %s", strerror(errno));
				fclose(f);
				return -1;
			}
		}
	}

	/* If not EOF and buffer filled, it's a truncation */
	if (!feof(f) && total == cap) {
		rc = 1; /* truncated */
	}

	buf[total] = '\0';
	if (out_len) *out_len = total;

	fclose(f);
	return rc;
}

int io_write_text(const char *path, const char *buf) {
	if (!path || !buf) {
		fprintf(stderr, "io_write_text: invalid arguments\n");
		return -1;
	}

	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "io_read_file_into: fopen %s", strerror(errno));
		return -1;
	}

	size_t len = 0;
	while (buf[len] != '\0') {
		++len;
	}

	size_t written = fwrite(buf, 1, len, f);
	if (written != len) {
		fprintf(stderr, "io_write_text: fwrite incomplete (wrote %zu of %zu bytes)\n", written, len);
		fclose(f);
		return -1;
	}

	if (fclose(f) != 0) {
		fprintf(stderr, "io_read_file_into: fclose %s", strerror(errno));
		return -1;
	}

	return 0;
}