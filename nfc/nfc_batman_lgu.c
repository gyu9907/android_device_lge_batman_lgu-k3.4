/*
 * Copyright (C) 2026 The LineageOS Project
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

#include <errno.h>
#include <malloc.h>
#include <string.h>

#include <hardware/hardware.h>
#include <hardware/nfc.h>

/*
 * PN544 EEPROM configuration recovered from the LG-F100L ICS NFC HAL.
 * Each entry is encoded as [0x00, address MSB, address LSB, value].
 */
static uint8_t batman_lgu_eeprom_settings[] = {
    0x00, 0x9b, 0xd1, 0x0d,
    0x00, 0x9b, 0xd2, 0x24,
    0x00, 0x9b, 0xd3, 0x0a,
    0x00, 0x9b, 0xd4, 0x22,
    0x00, 0x9b, 0xd5, 0x08,
    0x00, 0x9b, 0xd6, 0x1e,
    0x00, 0x9b, 0xdd, 0x1c,
    0x00, 0x9b, 0x84, 0x13,
    0x00, 0x99, 0x81, 0x7f,
    0x00, 0x99, 0x31, 0x70,
    0x00, 0x98, 0x00, 0x3f,
    0x00, 0x9f, 0x09, 0x00,
    0x00, 0x9f, 0x0a, 0x05,
    0x00, 0x9e, 0xd1, 0xa1,
    0x00, 0x99, 0x23, 0x00,
    0x00, 0x9e, 0x74, 0x80,
    0x00, 0x9e, 0x7d, 0x00,
    0x00, 0x9f, 0x28, 0x01,
    0x00, 0x9c, 0x31, 0x00,
    0x00, 0x9c, 0x32, 0xc8,
    0x00, 0x9c, 0x19, 0x40,
    0x00, 0x9c, 0x1a, 0x40,
    0x00, 0x9c, 0x0c, 0x00,
    0x00, 0x9c, 0x0d, 0x00,
    0x00, 0x9c, 0x12, 0x00,
    0x00, 0x9c, 0x13, 0x00,
    0x00, 0x98, 0xa2, 0x0e,
    0x00, 0x98, 0x93, 0x40,
    0x00, 0x98, 0x7d, 0x02,
    0x00, 0x98, 0x7e, 0x00,
    0x00, 0x9f, 0xc8, 0x01,
    0x00, 0x9f, 0x9a, 0x00,
    0x00, 0x9f, 0x09, 0x00,
};

static int pn544_close(hw_device_t *device)
{
    free(device);
    return 0;
}

static int nfc_open(const hw_module_t *module, const char *name,
        hw_device_t **device)
{
    nfc_pn544_device_t *pn544_device;

    if (strcmp(name, NFC_PN544_CONTROLLER) != 0)
        return -EINVAL;

    pn544_device = calloc(1, sizeof(*pn544_device));
    if (pn544_device == NULL)
        return -ENOMEM;

    pn544_device->common.tag = HARDWARE_DEVICE_TAG;
    pn544_device->common.version = 0;
    pn544_device->common.module = (struct hw_module_t *)module;
    pn544_device->common.close = pn544_close;

    pn544_device->num_eeprom_settings =
            sizeof(batman_lgu_eeprom_settings) / 4;
    pn544_device->eeprom_settings = batman_lgu_eeprom_settings;
    pn544_device->linktype = PN544_LINK_TYPE_I2C;
    pn544_device->device_node = "/dev/pn544";
    pn544_device->enable_i2c_workaround = 1;
    /* Zero selects libnfc-nxp's PN544 default I2C address, 0x57. */
    pn544_device->i2c_device_address = 0;

    *device = (hw_device_t *)pn544_device;
    return 0;
}

static struct hw_module_methods_t nfc_module_methods = {
    .open = nfc_open,
};

struct nfc_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 0,
        .id = NFC_HARDWARE_MODULE_ID,
        .name = "LG-F100L NFC HW HAL",
        .author = "The LineageOS Project",
        .methods = &nfc_module_methods,
    },
};
