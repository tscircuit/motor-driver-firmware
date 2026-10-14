#include "pico/bootrom.h"
#include "pico/unique_id.h"
#include "settings.h"
#include "tusb.h"
#include <string.h>
/* Pico SDK's CDC VID/PID. Hardware serial remains the board flash unique ID. */
static const tusb_desc_device_t device = {.bLength = sizeof(tusb_desc_device_t),
                                          .bDescriptorType = TUSB_DESC_DEVICE,
                                          .bcdUSB = 0x0200,
                                          .bDeviceClass = TUSB_CLASS_MISC,
                                          .bDeviceSubClass = MISC_SUBCLASS_COMMON,
                                          .bDeviceProtocol = MISC_PROTOCOL_IAD,
                                          .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
                                          .idVendor = 0x2e8a,
                                          .idProduct = 0x000a,
                                          .bcdDevice = 0x0100,
                                          .iManufacturer = 1,
                                          .iProduct = 2,
                                          .iSerialNumber = 3,
                                          .bNumConfigurations = 1};
uint8_t const *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
enum { ITF_CDC, ITF_CDC_DATA, ITF_COUNT };
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
static const uint8_t config[] = {TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, 0, 100),
                                 TUD_CDC_DESCRIPTOR(ITF_CDC, 4, 0x81, 8, 0x02, 0x82, 64)};
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { return config; }
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t buffer[64];
    static char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    if (!index) {
        buffer[0] = (TUSB_DESC_STRING << 8) | 4;
        buffer[1] = 0x0409;
        return buffer;
    }
    pico_get_unique_board_id_string(serial, sizeof(serial));
    for (size_t i = 0; serial[i]; ++i)
        if (serial[i] >= 'A' && serial[i] <= 'F')
            serial[i] += 'a' - 'A';
    const char *values[] = {NULL, "tscircuit", settings.name, serial, settings.name};
    if (index >= sizeof(values) / sizeof(values[0]))
        return NULL;
    size_t n = strlen(values[index]);
    if (n > 63)
        n = 63;
    for (size_t i = 0; i < n; ++i)
        buffer[i + 1] = (uint8_t)values[index][i];
    buffer[0] = (TUSB_DESC_STRING << 8) | (2 * n + 2);
    return buffer;
}
/* Standard Pico maintenance entry: close at 1200 baud to enter BOOTSEL.
   Main loop stops the motor before honoring this flag. */
volatile bool usb_boot_requested;
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    if (coding->bit_rate == 1200)
        usb_boot_requested = true;
}
