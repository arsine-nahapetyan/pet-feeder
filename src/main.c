#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <stdio.h>

/* OLED */
static const struct device *display_dev;

/* Keypad pins */
static const struct gpio_dt_spec rows[4] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(row0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(row1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(row2), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(row3), gpios),
};

static const struct gpio_dt_spec cols[3] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(col0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(col1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(col2), gpios),
};

/* Motor pins */
static const struct gpio_dt_spec step_pin = GPIO_DT_SPEC_GET(DT_ALIAS(step), gpios);
static const struct gpio_dt_spec dir_pin  = GPIO_DT_SPEC_GET(DT_ALIAS(dir),  gpios);
static const struct gpio_dt_spec ms1_pin  = GPIO_DT_SPEC_GET(DT_ALIAS(ms1),  gpios);
static const struct gpio_dt_spec ms2_pin  = GPIO_DT_SPEC_GET(DT_ALIAS(ms2),  gpios);
static const struct gpio_dt_spec ms3_pin  = GPIO_DT_SPEC_GET(DT_ALIAS(ms3),  gpios);

/* Keymap */
static const char keys[4][3] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

/* Time variables */
static int current_hour = 0;
static int current_min  = 0;
static int feed_hour    = -1;
static int feed_min     = -1;

/* Display helper */
void oled_show(const char *line1, const char *line2)
{
    cfb_framebuffer_clear(display_dev, true);
    cfb_print(display_dev, line1, 0, 0);
    if (line2) {
        cfb_print(display_dev, line2, 0, 16);
    }
    cfb_framebuffer_finalize(display_dev);
}

/* Show main screen */
void show_main(void)
{
    char line1[20];
    char line2[20];
    snprintf(line1, sizeof(line1), "Time: %02d:%02d", current_hour, current_min);
    if (feed_hour >= 0) {
        snprintf(line2, sizeof(line2), "Feed: %02d:%02d", feed_hour, feed_min);
    } else {
        snprintf(line2, sizeof(line2), "Feed: not set");
    }
    oled_show(line1, line2);
}

/* Motor feed */
void feed(int steps)
{
    gpio_pin_set_dt(&dir_pin, 1);
    for (int i = 0; i < steps; i++) {
        gpio_pin_set_dt(&step_pin, 1);
        k_usleep(1000);
        gpio_pin_set_dt(&step_pin, 0);
        k_usleep(1000);
    }
}

/* Scan keypad */
char scan_keypad(void)
{
    for (int r = 0; r < 4; r++) {
        gpio_pin_set_dt(&rows[r], 1);
        k_msleep(10);
        for (int c = 0; c < 3; c++) {
            if (gpio_pin_get_dt(&cols[c])) {
                gpio_pin_set_dt(&rows[r], 0);
                k_msleep(200);
                return keys[r][c];
            }
        }
        gpio_pin_set_dt(&rows[r], 0);
    }
    return 0;
}

/* Get 4 digit time input HHMM */
bool get_time_input(int *hour, int *min)
{
    char digits[4];
    int count = 0;
    char buf[20];

    oled_show("Enter time:", "HHMM + #");

    while (count < 4) {
        char key = scan_keypad();
        if (key >= '0' && key <= '9') {
            digits[count++] = key - '0';
            snprintf(buf, sizeof(buf), "-> %c%c:%c%c",
                count > 0 ? '0' + digits[0] : '-',
                count > 1 ? '0' + digits[1] : '-',
                count > 2 ? '0' + digits[2] : '-',
                count > 3 ? '0' + digits[3] : '-');
            oled_show("Enter time:", buf);
        }
        if (key == '#' && count == 4) {
            break;
        }
    }

    *hour = digits[0] * 10 + digits[1];
    *min  = digits[2] * 10 + digits[3];

    if (*hour > 23 || *min > 59) {
        oled_show("Invalid time!", NULL);
        k_msleep(2000);
        return false;
    }
    return true;
}

int main(void)
{
    /* Initialize OLED */
    display_dev = DEVICE_DT_GET_ONE(solomon_ssd1306);
    if (!device_is_ready(display_dev)) {
        return -1;
    }
    display_blanking_off(display_dev);
    cfb_framebuffer_init(display_dev);

    /* Initialize keypad */
    for (int i = 0; i < 4; i++) {
        gpio_pin_configure_dt(&rows[i], GPIO_OUTPUT_INACTIVE);
    }
    for (int i = 0; i < 3; i++) {
        gpio_pin_configure_dt(&cols[i], GPIO_INPUT | GPIO_PULL_DOWN);
    }

    /* Initialize motor */
    gpio_pin_configure_dt(&step_pin, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&dir_pin,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ms1_pin,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ms2_pin,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&ms3_pin,  GPIO_OUTPUT_INACTIVE);

    /* Half step mode */
    gpio_pin_set_dt(&ms1_pin, 1);
    gpio_pin_set_dt(&ms2_pin, 0);
    gpio_pin_set_dt(&ms3_pin, 0);

    show_main();

    int seconds = 0;

    while (1) {
        char key = scan_keypad();

        if (key == '#') {
            /* Manual feed */
            oled_show("Feeding...", NULL);
            feed(200);
            show_main();

        } else if (key == '0') {
            /* Set current time */
            int h, m;
            if (get_time_input(&h, &m)) {
                current_hour = h;
                current_min  = m;
                seconds = 0;
                oled_show("Time set!", NULL);
                k_msleep(1000);
                show_main();
            }

        } else if (key == '*') {
            /* Set feeding time */
            int h, m;
            if (get_time_input(&h, &m)) {
                feed_hour = h;
                feed_min  = m;
                oled_show("Feed time set!", NULL);
                k_msleep(1000);
                show_main();
            }
        }

        /* Update clock every second */
        k_msleep(1000);
        seconds++;
        if (seconds >= 60) {
            seconds = 0;
            current_min++;
            if (current_min >= 60) {
                current_min = 0;
                current_hour++;
                if (current_hour >= 24) {
                    current_hour = 0;
                }
            }
            show_main();
        }

        /* Check feeding time */
        if (feed_hour >= 0 &&
            current_hour == feed_hour &&
            current_min  == feed_min  &&
            seconds == 0) {
            oled_show("Auto feeding!", NULL);
            feed(200);
            show_main();
        }
    }

    return 0;
}