#ifndef SKNF_CMD_H
#define SKNF_CMD_H

#include "args.h"

int cmd_add(const struct Args* args);
int cmd_del(const struct Args* args);
int cmd_status(const struct Args* args);
int cmd_check(const struct Args* args);
int cmd_version(const struct Args* args);
int cmd_gc(const struct Args* args);

#endif