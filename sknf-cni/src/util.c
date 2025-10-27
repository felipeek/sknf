#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

void util_random_alphanumeric_6(char *buf) {
    static const char alnum[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    for (int i = 0; i < 6; i++) {
        buf[i] = alnum[rand() % (sizeof(alnum) - 1)];
    }
    buf[6] = '\0';
}