/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2022 Jacek Fedorynski
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <tusb.h>

#include "config.h"
#include "globals.h"
#include "our_descriptor.h"
#include "platform.h"
#include "remapper.h"

/*
  IMPORTANT (TinyUSB config):
    To enumerate with this descriptor set, your tusb_config.h must allow 4 HID instances:
      #define CFG_TUD_HID 4
    And buffers must support the largest endpoint (64 bytes):
      #define CFG_TUD_HID_EP_BUFSIZE 64   (or larger)
    Endpoint0 must be 64 bytes to match the target descriptor:
      #define CFG_TUD_ENDPOINT0_SIZE 64

  Notes:
    - This file intentionally uses fixed USB strings and a fixed serial to avoid leaking any MCU unique ID.
    - Index 5 is intentionally *not* implemented as a string descriptor to match USBTreeView's
      "String descriptor not found" for iInterface=5 on interface 3.
*/

#define USB_VID_TARGET 0x213F
#define USB_PID_TARGET 0x1109

// Device Descriptor (matches USBTreeView hexdump: 12 01 10 01 ... )
tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,

    .bcdUSB = 0x0110,            // USB 1.1
    .bDeviceClass = 0x00,        // defined by interfaces
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 0x40,     // 64 bytes

    .idVendor = USB_VID_TARGET,
    .idProduct = USB_PID_TARGET,
    .bcdDevice = 0x0000,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

// Configuration Descriptor (total length 0x74, 4 interfaces, 5 data endpoints)
static const uint8_t desc_configuration[] = {
    // Configuration Descriptor
    0x09, 0x02, 0x74, 0x00, 0x04, 0x01, 0x00, 0xA0, 0x32,

    // Interface 0: HID Boot Keyboard (EP 0x81 IN, 8 bytes, 1ms)
    0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3E, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x08, 0x00, 0x01,

    // Interface 1: HID Boot Mouse (EP 0x82 IN, 4 bytes, 1ms)
    0x09, 0x04, 0x01, 0x00, 0x01, 0x03, 0x01, 0x02, 0x00,
    0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0x2E, 0x00,
    0x07, 0x05, 0x82, 0x03, 0x04, 0x00, 0x01,

    // Interface 2: HID (EP 0x83 IN, 21 bytes, 1ms)
    0x09, 0x04, 0x02, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
    0x09, 0x21, 0x10, 0x01, 0x00, 0x01, 0x22, 0xA0, 0x00,
    0x07, 0x05, 0x83, 0x03, 0x15, 0x00, 0x01,

    // Interface 3: HID Vendor-defined IN/OUT (iInterface=5, but string 5 intentionally not provided)
    0x09, 0x04, 0x03, 0x00, 0x02, 0x03, 0x00, 0x00, 0x05,
    0x09, 0x21, 0x00, 0x01, 0x00, 0x01, 0x22, 0x22, 0x00,
    0x07, 0x05, 0x84, 0x03, 0x40, 0x00, 0x01,
    0x07, 0x05, 0x04, 0x03, 0x40, 0x00, 0x01,
};

// HID Report Descriptors (match USBTreeView)
static const uint8_t hid_report_itf0_keyboard[62] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x03, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x91, 0x02, 0x95, 0x05, 0x75, 0x01, 0x91, 0x01, 0x95, 0x06,
    0x75, 0x08, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x29, 0x91, 0x81, 0x00, 0xC0,
};

static const uint8_t hid_report_itf1_mouse[46] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x05,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
};

static const uint8_t hid_report_itf2_composite[160] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x19, 0x00, 0x29, 0x97, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x98, 0x81, 0x02, 0x05, 0x08, 0x19, 0x01, 0x29, 0x03, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x03, 0x91, 0x02, 0x75, 0x05, 0x95, 0x01, 0x91, 0x01, 0xC0, 0x05, 0x01, 0x09, 0x06, 0xA1,
    0x01, 0x85, 0x03, 0x05, 0x07, 0x15, 0x00, 0x25, 0x01, 0x19, 0x00, 0x29, 0x77, 0x95, 0x78, 0x75,
    0x01, 0x81, 0x02, 0xC0, 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x04, 0x05, 0x07, 0x19, 0x04,
    0x29, 0x70, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x78, 0x81, 0x02, 0xC0, 0x05, 0x01, 0x09,
    0x06, 0xA1, 0x01, 0x85, 0x05, 0x05, 0x07, 0x19, 0x00, 0x29, 0xE7, 0x15, 0x00, 0x26, 0xE7, 0x00,
    0x75, 0x08, 0x95, 0x14, 0x81, 0x00, 0xC0, 0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x15,
    0x00, 0x26, 0xFF, 0x1F, 0x19, 0x00, 0x2A, 0xFF, 0x1F, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
};

static const uint8_t hid_report_itf3_vendor[34] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x09, 0x02, 0x15, 0x00, 0x26, 0x00, 0xFF, 0x75, 0x08,
    0x95, 0x40, 0x81, 0x06, 0x09, 0x02, 0x15, 0x00, 0x26, 0x00, 0xFF, 0x75, 0x08, 0x95, 0x40, 0x91,
    0x06, 0xC0,
};

// String Descriptors (match USBTreeView; no unique ID injection)
static char const* string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },   // 0: 0x0409 English (United States)
    "dysm88",                       // 1: Manufacturer
    "HID_Keyboard",                 // 2: Product
    "1234567890",                   // 3: Serial
};

// -----------------------------------------------------------------------------
// TinyUSB callbacks
// -----------------------------------------------------------------------------

uint8_t const* tud_descriptor_device_cb() {
    return (uint8_t const*) &desc_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

uint8_t const* tud_hid_descriptor_report_cb(uint8_t itf) {
    switch (itf) {
        case 0: return hid_report_itf0_keyboard;
        case 1: return hid_report_itf1_mouse;
        case 2: return hid_report_itf2_composite;
        case 3: return hid_report_itf3_vendor;
        default: return NULL;
    }
}

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        // Note: index 0xEE is a Microsoft OS 1.0 Descriptor string. We intentionally
        // do not provide it here to match the target device behavior in USBTreeView.
        if (!(index < (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))) {
            return NULL;
        }

        const char* str = string_desc_arr[index];

        chr_count = (uint8_t) strlen(str);
        if (chr_count > 31) chr_count = 31;

        // Convert ASCII -> UTF-16LE
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = (uint8_t) str[i];
        }
    }

    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

// -----------------------------------------------------------------------------
// Optional HID report plumbing
//   - Keep existing application handlers for interface 0 (keyboard) and interface 3 (vendor),
//     and ignore others unless you add handlers.
// -----------------------------------------------------------------------------

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type,
                              uint8_t* buffer, uint16_t reqlen) {
    (void) report_type;

    if (itf == 0) {
        return handle_get_report0(report_id, buffer, reqlen);
    }
    if (itf == 3) {
        return handle_get_report1(report_id, buffer, reqlen);
    }

    // Not supported
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize) {
    (void) report_type;

    if (itf == 0) {
        // Preserve the existing "report_id in first byte" fallback used by the original code.
        if ((report_id == 0) && (report_type == 0) && (bufsize > 0)) {
            report_id = buffer[0];
            buffer++;
        }
        handle_set_report0(report_id, buffer, bufsize);
        return;
    }

    if (itf == 3) {
        handle_set_report1(report_id, buffer, bufsize);
        return;
    }

    // Ignore for other interfaces unless you implement handlers.
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    // Only track keyboard boot protocol on interface/instance 0.
    if (instance == 0) {
        boot_protocol_keyboard = (protocol == HID_PROTOCOL_BOOT);
        boot_protocol_updated = true;
    }
}

void tud_mount_cb() {
    reset_resolution_multiplier();
    if (boot_protocol_keyboard) {
        boot_protocol_keyboard = false;
        boot_protocol_updated = true;
    }
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    printf("tud_suspend_cb\n");
}

void tud_resume_cb() {
    printf("tud_resume_cb\n");
}
