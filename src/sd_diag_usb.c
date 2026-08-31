// SPDX-License-Identifier: CC0-1.0
// CDC-ACM descriptors for the SD diagnostic build's USB serial console.
#ifdef SD_DIAG_USB_CDC

#include "tusb.h"

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x2E8A,   // Raspberry Pi
    .idProduct          = 0x000A,   // Pico SDK CDC: binds cdc_acm cleanly
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

static const char* string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // supported language: English
    "EconoPET",
    "SD Diagnostic",
    "SDDIAG",
    "Diag console",
};

const uint8_t* tud_descriptor_device_cb(void) {
    return (const uint8_t*) &desc_device;
}

const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    static uint16_t desc_str[32];

    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
    if (index == 0) {
        memcpy(&desc_str[1], string_desc_arr[0], 2);
        desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | 4);
        return desc_str;
    }

    const char* str = string_desc_arr[index];
    uint8_t len = (uint8_t) strlen(str);
    if (len > 31) len = 31;
    for (uint8_t i = 0; i < len; i++) desc_str[1 + i] = str[i];
    desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc_str;
}

#endif // SD_DIAG_USB_CDC
