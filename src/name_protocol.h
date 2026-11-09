#ifndef BATON_NAME_PROTOCOL_H
#define BATON_NAME_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BATON_NAME_MAX_BYTES 20U
#define BATON_NAME_VALUE_MAX (2U + BATON_NAME_MAX_BYTES)
#define BATON_NAME_VERSION 1U

enum baton_name_status {
	BATON_NAME_SAVED = 0,
	BATON_NAME_SAVING = 1,
	BATON_NAME_FAILED = 2,
};

/* Valid UTF-8, no NUL/control characters or leading/trailing ASCII spaces. */
bool baton_name_valid(const uint8_t *name, size_t len);

#endif
