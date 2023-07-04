#include "tusb.h"
#include "usb_descriptors.h"
#include "pico/unique_id.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]  VIDEO | AUDIO | MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
    _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VIDEO, 5) | _PID_MAP(VENDOR, 6) )

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // Use Interface Association Descriptor (IAD) for Video
    // As required by USB Specs IAD's subclass must be common class (2) and protocol must be IAD (1)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCafe,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

#if CFG_TUD_VIDEO_STREAMING_BULK == 1
  #define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_VIDEO_CAPTURE_DESC_BULK_LEN  + CFG_TUD_CDC*TUD_CDC_DESC_LEN )
#else
  #define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_VIDEO_CAPTURE_DESC_LEN + CFG_TUD_CDC*TUD_CDC_DESC_LEN )
#endif

#define EPNUM_VIDEO_IN    0x81

#define EPNUM_CDC_NOTIF   0x83
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

uint8_t const desc_fs_configuration[] = {
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 500),
  // IAD for Video Control
  #if CFG_TUD_VIDEO_STREAMING_BULK == 1
    TUD_VIDEO_CAPTURE_DESCRIPTOR_BULK(4, EPNUM_VIDEO_IN,
                               FRAME_WIDTH, FRAME_HEIGHT, FRAME_RATE, FRAME_RATE_STEP, FRAME_RATE_STEP_INTERVAL,
                               64),
  #else
    TUD_VIDEO_CAPTURE_DESCRIPTOR(4, EPNUM_VIDEO_IN,
                               FRAME_WIDTH, FRAME_HEIGHT, FRAME_RATE, FRAME_RATE_STEP, FRAME_RATE_STEP_INTERVAL,
                               CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE),
  #endif
  // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
  #if CFG_TUD_CDC == 1
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
  #endif
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
  (void) index; // for multiple configurations
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char * string_desc_arr [] =
{
  (char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
  "there.oughta.be",             // 1: Manufacturer
  "GB Interceptor",              // 2: Product
  "1234567890123456",            // 3: Serials, should use chip ID
  "GB Interceptor Video",        // 4: UVC Interface
};

static uint16_t _desc_str[32];

void setUniqueSerial() {
    pico_get_unique_board_id_string(string_desc_arr[3], 17);
}

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;

  uint8_t chr_count;

  if ( index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

    if ( !(index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) ) return NULL;

    const char* str = string_desc_arr[index];

    // Cap at max char
    chr_count = (uint8_t) strlen(str);
    if ( chr_count > 31 ) chr_count = 31;

    // Convert ASCII string into UTF-16
    for(uint8_t i=0; i<chr_count; i++) {
      _desc_str[1+i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8 ) | (2*chr_count + 2));

  return _desc_str;
}