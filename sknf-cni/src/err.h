#ifndef SKNF_ERR_H
#define SKNF_ERR_H

#define ERR_MSG_MAX_SIZE 1024
#define ERR_DETAILS_MAX_SIZE 1024

typedef struct Err {
    int initialized;
    int code;
    char msg[ERR_MSG_MAX_SIZE];
    char details[ERR_DETAILS_MAX_SIZE];
} Err;

void ERR_INIT(Err* err);
void ERRF(Err* err, const char* msg_fmt, const char* details_fmt, ...);
void ERR(Err* err, const char* fmt, ...);

#endif