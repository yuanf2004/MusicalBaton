#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

enum button_event_type {
	BUTTON_EVENT_CLICKS,
	BUTTON_EVENT_LONG_HOLD,
};

struct button_event {
	enum button_event_type type;
	uint8_t click_count;
};

typedef void (*button_event_handler_t)(const struct button_event *event);

int button_init(button_event_handler_t handler);

#endif