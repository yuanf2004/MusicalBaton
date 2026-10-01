#include "button.h"

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BUTTON_NODE DT_ALIAS(baton_button)

#define DEBOUNCE_TIME   K_MSEC(30)
#define CLICK_WINDOW    K_MSEC(600)
#define LONG_HOLD_TIME  K_SECONDS(10)

static const struct gpio_dt_spec button =
	GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

static struct gpio_callback button_callback;
static struct k_work_delayable debounce_work;
static struct k_work_delayable click_work;
static struct k_work_delayable long_hold_work;

static button_event_handler_t event_handler;

static bool pressed;
static bool long_hold_reported;
static uint8_t click_count;

static void report_event(enum button_event_type type, uint8_t count)
{
	struct button_event event = {
		.type = type,
		.click_count = count,
	};

	if (event_handler != NULL) {
		event_handler(&event);
	}
}

static void click_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (click_count > 0) {
		report_event(BUTTON_EVENT_CLICKS, click_count);
		click_count = 0;
	}
}

static void long_hold_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Confirm that the button is still held. */
	if (gpio_pin_get_dt(&button) == 1) {
		long_hold_reported = true;
		click_count = 0;
		k_work_cancel_delayable(&click_work);

		report_event(BUTTON_EVENT_LONG_HOLD, 0);
	}
}

static void debounce_work_handler(struct k_work *work)
{
	bool now_pressed;

	ARG_UNUSED(work);

	now_pressed = gpio_pin_get_dt(&button) == 1;

	if (now_pressed == pressed) {
		return;
	}

	pressed = now_pressed;

	if (pressed) {
		report_event(BUTTON_EVENT_PRESSED, 0);
		long_hold_reported = false;
		k_work_reschedule(&long_hold_work, LONG_HOLD_TIME);
		return;
	}

	k_work_cancel_delayable(&long_hold_work);

	if (long_hold_reported) {
		long_hold_reported = false;
		return;
	}

	click_count++;

	/*
	 * Restart the window after every click. Once no additional click
	 * arrives for 600 ms, the complete click count is reported.
	 */
	k_work_reschedule(&click_work, CLICK_WINDOW);
}

static void button_interrupt(
	const struct device *device,
	struct gpio_callback *callback,
	gpio_port_pins_t pins)
{
	ARG_UNUSED(device);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);

	k_work_reschedule(&debounce_work, DEBOUNCE_TIME);
}

int button_init(button_event_handler_t handler)
{
	int ret;

	if (!gpio_is_ready_dt(&button)) {
		printk("Button GPIO is not ready\n");
		return -ENODEV;
	}

	event_handler = handler;

	k_work_init_delayable(&debounce_work, debounce_work_handler);
	k_work_init_delayable(&click_work, click_work_handler);
	k_work_init_delayable(&long_hold_work, long_hold_work_handler);

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	gpio_init_callback(
		&button_callback,
		button_interrupt,
		BIT(button.pin)
	);

	ret = gpio_add_callback(button.port, &button_callback);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(
		&button,
		GPIO_INT_EDGE_BOTH
	);

	if (ret < 0) {
		return ret;
	}

	printk("Button initialized: %s pin %u\n",
	       button.port->name, button.pin);
	return 0;
}
