#include "err.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void vfmt_into(char* dst, size_t cap, const char* fmt, va_list ap) {
	if (!dst || cap == 0) return;
	if (fmt && *fmt) {
		vsnprintf(dst, cap, fmt, ap);
		dst[cap - 1] = '\0';
	} else {
		dst[0] = '\0';
	}
}

void ERR_INIT(Err* err) {
	if (!err) return;
	memset(err, 0, sizeof(*err));
}

void ERRF(Err* err, const char* msg_fmt, const char* details_fmt, ...) {
	if (!err) return;

	ERR_INIT(err);
	err->initialized = 1;
	err->code = 100;

	va_list ap;
	va_start(ap, details_fmt);

	// Both format strings receive the SAME varargs list.
	// This requires the arguments to match both formats (common C pattern).
	va_list ap_copy;
	va_copy(ap_copy, ap);
	vfmt_into(err->msg, sizeof(err->msg), msg_fmt, ap_copy);
	va_end(ap_copy);

	vfmt_into(err->details, sizeof(err->details), details_fmt, ap);
	va_end(ap);
}

void ERR(Err* err, const char* fmt, ...) {
	if (!err) return;

	ERR_INIT(err);
	err->initialized = 1;
	err->code = 100;

	va_list ap;
	va_start(ap, fmt);
	vfmt_into(err->msg, sizeof(err->msg), fmt, ap);
	va_end(ap);

	err->details[0] = '\0';
}
