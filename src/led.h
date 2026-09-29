#ifndef MUSICAL_BATON_LED_H
#define MUSICAL_BATON_LED_H

#include <stdbool.h>
#include <stdint.h>

enum led_bluetooth_state {
	LED_BLUETOOTH_OFF,
	LED_BLUETOOTH_ADVERTISING,
	LED_BLUETOOTH_CONNECTED,
};

enum led_battery_state {
	LED_BATTERY_OFF,
	LED_BATTERY_GOOD,
	LED_BATTERY_LOW,
	LED_BATTERY_CRITICAL,
	LED_BATTERY_CHARGING,
};

int led_init(void);
void led_set_bluetooth_state(enum led_bluetooth_state state);
void led_set_battery_state(enum led_battery_state state);

/* Charging is blue; >50% green; 20-50% yellow; below 20% red. */
void led_set_battery_level(uint8_t percentage, bool charging);

#endif
