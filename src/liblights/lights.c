/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2017 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * State/handler organization follows the Apache-2.0 LineageOS references:
 * mako: 5b211791f83b63a22c0d6055d256de71b484345b, liblight/lights.c
 * g2-common: 5e4b5a576e27597ede4628f9259934583afb0209, lights/Light.cpp
 * Hardware behavior is reconstructed from batman's original lights.msm8660.so
 * SHA256 bddcf3e92153d68cffbd4394b3207fe30c53c98a3f3fcf2937d5dbb9a18da5f9.
 * Do not substitute Mako's LED PWM/lock or G2's gamma/blink-pattern interfaces.
 */
#define LOG_TAG "lights.msm8660"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hardware/lights.h>
#include <log/log.h>

static const char LCD_FILE[] = "/sys/class/leds/lcd-backlight/brightness";
static const char BUTTON_FILE[] = "/sys/class/leds/button-backlight/brightness";
/* This kernel name denotes the power-button LED, not a physical keyboard. */
static const char KEYBOARD_FILE[] = "/sys/class/leds/keyboard-backlight/brightness";
static const char RED_FILE[] = "/sys/class/leds/red/brightness";
static const char GREEN_FILE[] = "/sys/class/leds/green/brightness";
static const char BLUE_FILE[] = "/sys/class/leds/blue/brightness";

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock;
static struct {
    struct light_state_t battery;
    struct light_state_t notification;
    int32_t attention;
    uint32_t buttons;
} g_state;
static int g_warned;

static void initialize(void)
{
    pthread_mutex_init(&g_lock, NULL);
}

/* Preserve the blob's single-record I/O and legacy return-value convention.
 * In particular, short writes and close failures did not change a successful
 * write's result. Shared RGB callers below intentionally ignore this result.
 */
static int write_node(const char *path, int32_t value)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        if (!g_warned) {
            ALOGE("write_int failed to open %s", path);
            g_warned = 1;
        }
        return -errno;
    }
    char buffer[20];
    int length = snprintf(buffer, sizeof(buffer), "%d\n", value);
    ssize_t written = write(fd, buffer, (size_t)length);
    close(fd);
    return written == -1 ? -errno : 0;
}

static int32_t two_level(uint32_t component)
{
    return component > 128 ? 2 : component != 0;
}

static void write_shared_rgb(const struct light_state_t *state)
{
    int32_t red = two_level((state->color >> 16) & 255);
    int32_t green = two_level((state->color >> 8) & 255);
    int32_t blue = state->color & 255;
    /* The original ARM code's low-blue branch changes RED to 1 and leaves
     * BLUE at 1..128. Preserve this observable quirk rather than "fixing" it.
     * Blob: 0xa94..0xaa6, confirmed by emulated syscall traces.
     */
    if (blue > 128)
        blue = 2;
    else if (blue != 0)
        red = 1;

    write_node(RED_FILE, red);
    write_node(GREEN_FILE, green);
    write_node(BLUE_FILE, blue);
    if (state->flashMode == LIGHT_FLASH_TIMED &&
        state->flashOnMS > 0 && state->flashOffMS > 0) {
        /* ARM adds modulo 2^32 before signed division. No C signed overflow. */
        int32_t total = (int32_t)((uint32_t)state->flashOnMS +
                                 (uint32_t)state->flashOffMS);
        /* The blob writes the period to red/brightness, not a PWM node. */
        write_node(RED_FILE, total / 50);
    }
}

static void update_shared(void)
{
    /* Blob 0xafc selects the battery state before the notification state. */
    write_shared_rgb((g_state.battery.color & 0x00ffffff) ?
                     &g_state.battery : &g_state.notification);
}

static int set_backlight(struct light_device_t *device,
                         const struct light_state_t *state)
{
    (void)device;
    if (!state) return -EINVAL;
    uint32_t color = state->color;
    int32_t value = (77 * ((color >> 16) & 255) +
                     150 * ((color >> 8) & 255) + 29 * (color & 255)) >> 8;
    pthread_mutex_lock(&g_lock);
    int result = write_node(LCD_FILE, value);
    pthread_mutex_unlock(&g_lock);
    return result;
}

static int set_buttons(struct light_device_t *device,
                       const struct light_state_t *state)
{
    (void)device;
    if (!state) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    g_state.buttons = state->color & 0x00ffffff;
    int result = write_node(BUTTON_FILE, g_state.buttons ? 255 : 0);
    pthread_mutex_unlock(&g_lock);
    return result;
}

static int set_keyboard(struct light_device_t *device,
                        const struct light_state_t *state)
{
    (void)device;
    if (!state) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    int result = write_node(KEYBOARD_FILE, (state->color & 0x00ffffff) ? 255 : 0);
    pthread_mutex_unlock(&g_lock);
    return result;
}

static int set_battery(struct light_device_t *device,
                       const struct light_state_t *state)
{
    (void)device;
    if (!state) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    g_state.battery = *state;
    update_shared();
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_notification(struct light_device_t *device,
                            const struct light_state_t *state)
{
    (void)device;
    if (!state) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    g_state.notification = *state;
    update_shared();
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int set_attention(struct light_device_t *device,
                         const struct light_state_t *state)
{
    (void)device;
    if (!state) return -EINVAL;
    pthread_mutex_lock(&g_lock);
    if (state->flashMode == LIGHT_FLASH_HARDWARE)
        g_state.attention = state->flashOnMS;
    else if (state->flashMode == LIGHT_FLASH_NONE)
        g_state.attention = 0;
    /* The blob stores attention but does not use it to select RGB output. */
    update_shared();
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int close_lights(struct hw_device_t *device)
{
    free(device);
    return 0;
}

static const struct {
    const char *name;
    int (*set)(struct light_device_t *, const struct light_state_t *);
} handlers[] = {
    { LIGHT_ID_BACKLIGHT, set_backlight },
    { LIGHT_ID_KEYBOARD, set_keyboard },
    { LIGHT_ID_BUTTONS, set_buttons },
    { LIGHT_ID_BATTERY, set_battery },
    { LIGHT_ID_NOTIFICATIONS, set_notification },
    { LIGHT_ID_ATTENTION, set_attention },
};

static int open_lights(const struct hw_module_t *module, const char *name,
                       struct hw_device_t **device)
{
    if (!device) return -EINVAL;
    *device = NULL;
    if (!module || !name) return -EINVAL;
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); ++i) {
        if (strcmp(name, handlers[i].name) != 0) continue;
        pthread_once(&g_once, initialize);
        struct light_device_t *light = calloc(1, sizeof(*light));
        if (!light) return -ENOMEM;
        light->common.tag = HARDWARE_DEVICE_TAG;
        light->common.version = 0;
        light->common.module = (struct hw_module_t *)module;
        light->common.close = close_lights;
        light->set_light = handlers[i].set;
        *device = &light->common;
        return 0;
    }
    return -EINVAL;
}

static struct hw_module_methods_t methods = { .open = open_lights };
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "LGE batman lights module",
    .author = "The Android Open Source Project, The LineageOS Project",
    .methods = &methods,
};
