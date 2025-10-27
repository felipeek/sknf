#ifndef SKNF_IO_H
#define SKNF_IO_H
#include <stddef.h>

int io_file_exists(const char *path);
int io_read_file_into(const char *path, char *buf, size_t bufsize, size_t *out_len);
int io_write_text(const char *path, const char *buf);

#endif