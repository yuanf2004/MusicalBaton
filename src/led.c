#include "led.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define FADE_UPDATE_INTERVAL K_MSEC(60)
#define FADE_STEPS 25U
#define BLE_LED_NODE DT_ALIAS(ble_led)

BUILD_ASSERT(DT_NODE_HAS_STATUS(BLE_LED_NODE, okay),
	     "The board overlay must define the ble-led alias");

static const struct pwm_dt_spec bluetooth_led =
	PWM_DT_SPEC_GET(BLE_LED_NODE);

/* RGB aliases exist on the custom PCB but are optional for development. */
#if DT_NODE_HAS_STATUS(DT_ALIAS(rgb_red_led), okay) && \
	DT_NODE_HAS_STATUS(DT_ALIAS(rgb_green_led), okay) && \
	DT_NODE_HAS_STATUS(DT_ALIAS(rgb_blue_led), okay)
#define HAS_BATTERY_RGB 1
static const struct gpio_dt_spec rgb_red_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(rgb_red_led), gpios);
static const struct gpio_dt_spec rgb_green_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(rgb_green_led), gpios);
static const struct gpio_dt_spec rgb_blue_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(rgb_blue_led), gpios);
#else
#define HAS_BATTERY_RGB 0
#endif

static enum led_bluetooth_state bluetooth_state;
static uint8_t fade_step;
static bool fade_up;
static bool initialized;
static K_MUTEX_DEFINE(led_mutex);

static void bluetooth_fade_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(bluetooth_fade_work,
			       bluetooth_fade_handler);

static void set_bluetooth_brightness(uint8_t step)
{
	uint32_t pulse_width;
	int ret;

	if (step > FADE_STEPS) {
		step = FADE_STEPS;
	}

	pulse_width = (bluetooth_led.period * step) / FADE_STEPS;
	ret = pwm_set_pulse_dt(&bluetooth_led, pulse_width);
	if (ret < 0) {
		printk("Failed to update Bluetooth LED: %d\n", ret);
	}
}

#if HAS_BATTERY_RGB
static void set_gpio(const struct gpio_dt_spec *led, bool on)
{
	int ret = gpio_pin_set_dt(led, on ? 1 : 0);

	if (ret < 0) {
		printk("Failed to update battery LED: %d\n", ret);
	}
}

static int configure_gpio_led(const struct gpio_dt_spec *led)
{
	if (!gpio_is_ready_dt(led)) {
		return -ENODEV;
	}

	return gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
}
#endif

static void bluetooth_fade_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&led_mutex, K_FOREVER);

	if (initialized &&
	    bluetooth_state == LED_BLUETOOTH_ADVERTISING) {
		set_bluetooth_brightness(fade_step);

		if (fade_up) {
			if (fade_step >= FADE_STEPS) {
				fade_up = false;
				fade_step--;
			} else {
				fade_step++;
			}
		} else if (fade_step == 0U) {
			fade_up = true;
			fade_step++;
		} else {
			fade_step--;
		}

		k_work_reschedule(&bluetooth_fade_work,
				  FADE_UPDATE_INTERVAL);
	}

	k_mutex_unlock(&led_mutex);
}

int led_init(void)
{
	int ret;

	if (!pwm_is_ready_dt(&bluetooth_led)) {
		printk("Bluetooth LED PWM is not ready\n");
		return -ENODEV;
	}

	ret = pwm_set_pulse_dt(&bluetooth_led, 0U);
	if (ret < 0) {
		printk("Bluetooth LED initialization failed: %d\n", ret);
		return ret;
	}

#if HAS_BATTERY_RGB
	ret = configure_gpio_led(&rgb_red_led);
	if (ret < 0) {
		return ret;
	}
	ret = configure_gpio_led(&rgb_green_led);
	if (ret < 0) {
		return ret;
	}
	ret = configure_gpio_led(&rgb_blue_led);
	if (ret < 0) {
		return ret;
	}
#endif

	bluetooth_state = LED_BLUETOOTH_OFF;
	fade_step = 0U;
	fade_up = true;
	initialized = true;
	return 0;
}

void led_set_bluetooth_state(enum led_bluetooth_state state)
{
	if (!initialized) {
		return;
	}

	k_mutex_lock(&led_mutex, K_FOREVER);
	k_work_cancel_delayable(&bluetooth_fade_work);
	bluetooth_state = state;

	switch (state) {
	case LED_BLUETOOTH_ADVERTISING:
		fade_step = 0U;
		fade_up = true;
		set_bluetooth_brightness(fade_step);
		k_work_reschedule(&bluetooth_fade_work, K_NO_WAIT);
		break;
	case LED_BLUETOOTH_CONNECTED:
		set_bluetooth_brightness(FADE_STEPS);
		break;
	case LED_BLUETOOTH_OFF:
	default:
		set_bluetooth_brightness(0U);
		break;
	}

	k_mutex_unlock(&led_mutex);
}

void led_set_battery_state(enum led_battery_state state)
{
#if HAS_BATTERY_RGB
	bool red = false;
	bool green = false;
	bool blue = false;

	if (!initialized) {
		return;
	}

	switch (state) {
	case LED_BATTERY_GOOD:
		green = true;
		break;
	case LED_BATTERY_LOW:
		red = true;
		green = true;
		break;
	case LED_BATTERY_CRITICAL:
		red = true;
		break;
	case LED_BATTERY_CHARGING:
		blue = true;
		break;
	case LED_BATTERY_OFF:
	default:
		break;
	}

	k_mutex_lock(&led_mutex, K_FOREVER);
	set_gpio(&rgb_red_led, red);
	set_gpio(&rgb_green_led, green);
	set_gpio(&rgb_blue_led, blue);
	k_mutex_unlock(&led_mutex);
#else
	ARG_UNUSED(state);
#endif
}

void led_set_battery_level(uint8_t percentage, bool charging)
{
	if (charging) {
		led_set_battery_state(LED_BATTERY_CHARGING);
	} else if (percentage < 20U) {
		led_set_battery_state(LED_BATTERY_CRITICAL);
	} else if (percentage <= 50U) {
		led_set_battery_state(LED_BATTERY_LOW);
	} else {
		led_set_battery_state(LED_BATTERY_GOOD);
	}
}
