#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define SLEEP_TIME_MS 250

#define RED_NODE   DT_NODELABEL(red_led)
#define GREEN_NODE DT_NODELABEL(green_led)
#define BLUE_NODE  DT_NODELABEL(blue_led)

static const struct gpio_dt_spec red =
    GPIO_DT_SPEC_GET(RED_NODE, gpios);

static const struct gpio_dt_spec green =
    GPIO_DT_SPEC_GET(GREEN_NODE, gpios);

static const struct gpio_dt_spec blue =
    GPIO_DT_SPEC_GET(BLUE_NODE, gpios);

int main(void)
{
    if (!gpio_is_ready_dt(&red) ||
        !gpio_is_ready_dt(&green) ||
        !gpio_is_ready_dt(&blue)) {
        return 0;
    }

    gpio_pin_configure_dt(&red, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&green, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&blue, GPIO_OUTPUT_INACTIVE);

   while (1) {
    /* RED */
    gpio_pin_set_dt(&red, 1);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* OFF */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* GREEN */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 1);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* OFF */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* BLUE */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 1);
    k_msleep(SLEEP_TIME_MS);

    /* OFF */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* YELLOW */
    gpio_pin_set_dt(&red, 1);
    gpio_pin_set_dt(&green, 1);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* OFF */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* MAGENTA */
    gpio_pin_set_dt(&red, 1);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 1);
    k_msleep(SLEEP_TIME_MS);

    /* OFF */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* CYAN */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 1);
    gpio_pin_set_dt(&blue, 1);
    k_msleep(SLEEP_TIME_MS);

    /* OFF */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);

    /* WHITE */
    gpio_pin_set_dt(&red, 1);
    gpio_pin_set_dt(&green, 1);
    gpio_pin_set_dt(&blue, 1);
    k_msleep(SLEEP_TIME_MS);

    /* OFF */
    gpio_pin_set_dt(&red, 0);
    gpio_pin_set_dt(&green, 0);
    gpio_pin_set_dt(&blue, 0);
    k_msleep(SLEEP_TIME_MS);
}

    return 0;
}