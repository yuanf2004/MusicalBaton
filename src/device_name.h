#ifndef BATON_DEVICE_NAME_H
#define BATON_DEVICE_NAME_H

#include "name_protocol.h"

int baton_device_name_init(void);
int baton_device_name_request(const uint8_t *name, size_t len);
/* Read value: version, persistence status, followed by last committed UTF-8 name. */
size_t baton_device_name_snapshot(uint8_t value[BATON_NAME_VALUE_MAX]);
void baton_device_name_copy(char name[BATON_NAME_MAX_BYTES + 1]);

#endif
