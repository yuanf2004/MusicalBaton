#pragma once
#include <stdbool.h>
#include <stdint.h>
void practice_command(bool active, uint16_t sequence);
void practice_request_toggle(void);
void practice_snapshot(uint8_t packet[6]);
bool practice_update(void);
void practice_disconnect(void);

void practice_set_calibrated(bool calibrated);
