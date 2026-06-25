#include "product_status_io.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(product_status_io, LOG_LEVEL_INF);

#define GREEN_LED_NODE DT_ALIAS(status_green_led)
#define RED_LED_NODE DT_ALIAS(status_red_led)
#define BUTTON_NODE DT_ALIAS(power_button)

#define STATUS_PERIOD_MS 100
#define BOOT_BLINK_MS 500
#define ACTIVITY_WINDOW_MS 3000
#define ACTIVITY_BLINK_MIN_MS 80
#define ACTIVITY_BLINK_MAX_MS 600
#define LONG_PRESS_MS 1800
#define CONFIG_RESET_PRESS_MS 10000

enum status_io_state {
    STATUS_IO_BOOTING = 0,
    STATUS_IO_STOPPED,
    STATUS_IO_RUNNING,
};

static const struct gpio_dt_spec green_led = GPIO_DT_SPEC_GET_OR(GREEN_LED_NODE, gpios, {0});
static const struct gpio_dt_spec red_led = GPIO_DT_SPEC_GET_OR(RED_LED_NODE, gpios, {0});
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(BUTTON_NODE, gpios, {0});

static struct gpio_callback button_cb;
static struct k_work_delayable status_work;
static struct k_work_delayable button_work;
static product_status_button_fn button_long_press_fn;
static void *button_ctx;
static product_status_button_fn config_reset_fn;
static void *config_reset_ctx;
static uint8_t io_ready;
static uint8_t activity_led_on;
static uint8_t long_press_reported;
static uint8_t config_reset_reported;
static enum status_io_state status_state = STATUS_IO_BOOTING;
static uint32_t activity_count;
static int64_t activity_window_start_ms;
static int64_t last_toggle_ms;
static int64_t button_pressed_ms;

static void set_led(const struct gpio_dt_spec *led, int value)
{
    if (led->port) {
        (void)gpio_pin_set_dt(led, value);
    }
}

static uint32_t blink_interval_ms(int64_t now)
{
    int64_t elapsed = now - activity_window_start_ms;
    uint32_t rate_per_sec;
    uint32_t interval;

    if (elapsed <= 0 || activity_count == 0) {
        return ACTIVITY_BLINK_MAX_MS;
    }

    if (elapsed > ACTIVITY_WINDOW_MS) {
        activity_count = 0;
        activity_window_start_ms = now;
        return ACTIVITY_BLINK_MAX_MS;
    }

    rate_per_sec = (uint32_t)((activity_count * 1000U) / (uint32_t)elapsed);
    if (rate_per_sec == 0) {
        return ACTIVITY_BLINK_MAX_MS;
    }

    interval = 1000U / rate_per_sec;
    if (interval < ACTIVITY_BLINK_MIN_MS) {
        interval = ACTIVITY_BLINK_MIN_MS;
    }
    if (interval > ACTIVITY_BLINK_MAX_MS) {
        interval = ACTIVITY_BLINK_MAX_MS;
    }
    return interval;
}

static void status_work_handler(struct k_work *work)
{
    int64_t now = k_uptime_get();

    ARG_UNUSED(work);

    if (!io_ready) {
        return;
    }

    if (status_state == STATUS_IO_BOOTING) {
        if (now - last_toggle_ms >= BOOT_BLINK_MS) {
            activity_led_on = !activity_led_on;
            set_led(&red_led, activity_led_on);
            set_led(&green_led, 0);
            last_toggle_ms = now;
        }
    } else if (status_state == STATUS_IO_STOPPED) {
        set_led(&green_led, 0);
        set_led(&red_led, 1);
        activity_led_on = 0;
    } else {
        set_led(&red_led, 0);
        if (activity_count == 0 || now - activity_window_start_ms > ACTIVITY_WINDOW_MS) {
            activity_count = 0;
            activity_led_on = 1;
            set_led(&green_led, 1);
        } else if (now - last_toggle_ms >= blink_interval_ms(now)) {
            activity_led_on = !activity_led_on;
            set_led(&green_led, activity_led_on);
            last_toggle_ms = now;
        }
    }

    k_work_schedule(&status_work, K_MSEC(STATUS_PERIOD_MS));
}

static void button_work_handler(struct k_work *work)
{
    int value;
    int64_t now;

    ARG_UNUSED(work);

    if (!io_ready || !button.port) {
        return;
    }

    value = gpio_pin_get_dt(&button);
    now = k_uptime_get();
    if (value > 0) {
        if (button_pressed_ms == 0) {
            button_pressed_ms = now;
            long_press_reported = 0;
            config_reset_reported = 0;
        }
        if (status_state == STATUS_IO_RUNNING &&
            !config_reset_reported &&
            now - button_pressed_ms >= CONFIG_RESET_PRESS_MS) {
            config_reset_reported = 1;
            long_press_reported = 1;
            LOG_INF("power button held for config reset");
            if (config_reset_fn) {
                config_reset_fn(config_reset_ctx);
            }
        }
        k_work_schedule(&button_work, K_MSEC(100));
    } else {
        if (!config_reset_reported &&
            !long_press_reported &&
            button_pressed_ms != 0 &&
            now - button_pressed_ms >= LONG_PRESS_MS) {
            long_press_reported = 1;
            if (button_long_press_fn) {
                button_long_press_fn(button_ctx);
            }
        }
        button_pressed_ms = 0;
        long_press_reported = 0;
        config_reset_reported = 0;
    }
}

static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    k_work_schedule(&button_work, K_NO_WAIT);
}

static void configure_button(const struct gpio_dt_spec *spec,
                             struct gpio_callback *cb,
                             gpio_callback_handler_t isr,
                             const char *name)
{
    int rc;

    if (!spec->port) {
        return;
    }
    if (!gpio_is_ready_dt(spec)) {
        LOG_WRN("%s GPIO not ready", name);
        return;
    }
    rc = gpio_pin_configure_dt(spec, GPIO_INPUT);
    if (rc != 0) {
        LOG_WRN("%s configure failed: %d", name, rc);
        return;
    }
    rc = gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_BOTH);
    if (rc != 0) {
        LOG_WRN("%s interrupt configure failed: %d", name, rc);
        return;
    }
    gpio_init_callback(cb, isr, BIT(spec->pin));
    (void)gpio_add_callback(spec->port, cb);
}

int product_status_io_init(product_status_button_fn power_long_press_fn,
                           void *power_ctx,
                           product_status_button_fn config_reset_cb,
                           void *reset_ctx)
{
    int rc;

    button_long_press_fn = power_long_press_fn;
    button_ctx = power_ctx;
    config_reset_fn = config_reset_cb;
    config_reset_ctx = reset_ctx;
    activity_window_start_ms = k_uptime_get();
    last_toggle_ms = activity_window_start_ms;
    status_state = STATUS_IO_BOOTING;

    if (green_led.port) {
        if (!gpio_is_ready_dt(&green_led)) {
            LOG_WRN("green LED GPIO not ready");
        } else {
            rc = gpio_pin_configure_dt(&green_led, GPIO_OUTPUT_INACTIVE);
            if (rc != 0) {
                LOG_WRN("green LED configure failed: %d", rc);
            }
        }
    }

    if (red_led.port) {
        if (!gpio_is_ready_dt(&red_led)) {
            LOG_WRN("red LED GPIO not ready");
        } else {
            rc = gpio_pin_configure_dt(&red_led, GPIO_OUTPUT_ACTIVE);
            if (rc != 0) {
                LOG_WRN("red LED configure failed: %d", rc);
            }
        }
    }

    k_work_init_delayable(&status_work, status_work_handler);
    k_work_init_delayable(&button_work, button_work_handler);

    configure_button(&button, &button_cb, button_isr, "power button");

    io_ready = 1;
    set_led(&green_led, 0);
    set_led(&red_led, 1);
    LOG_INF("status IO ready: booting, green=GPIO%u red=GPIO%u power=GPIO%u",
            green_led.pin, red_led.pin, button.pin);
    k_work_schedule(&status_work, K_NO_WAIT);
    return 0;
}

void product_status_io_set_running(uint8_t is_running)
{
    status_state = is_running ? STATUS_IO_RUNNING : STATUS_IO_STOPPED;
    activity_count = 0;
    activity_led_on = is_running ? 1 : 0;
    last_toggle_ms = k_uptime_get();
    if (is_running) {
        set_led(&red_led, 0);
        set_led(&green_led, 1);
        LOG_INF("status IO state: running");
    } else {
        set_led(&green_led, 0);
        set_led(&red_led, 1);
        LOG_INF("status IO state: stopped");
    }
    if (io_ready) {
        k_work_schedule(&status_work, K_NO_WAIT);
    }
}

void product_status_io_record_activity(void)
{
    int64_t now = k_uptime_get();

    if (!io_ready) {
        return;
    }
    if (status_state != STATUS_IO_RUNNING) {
        return;
    }
    if (now - activity_window_start_ms > ACTIVITY_WINDOW_MS) {
        activity_window_start_ms = now;
        activity_count = 0;
    }
    if (activity_count < UINT32_MAX) {
        activity_count++;
    }
}
