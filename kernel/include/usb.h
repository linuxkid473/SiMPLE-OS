#ifndef SIMPLE_USB_H
#define SIMPLE_USB_H

#include "types.h"

/* USB descriptor types */
#define USB_DT_DEVICE    0x01
#define USB_DT_CONFIG    0x02
#define USB_DT_STRING    0x03
#define USB_DT_INTERFACE 0x04
#define USB_DT_ENDPOINT  0x05
#define USB_DT_HID       0x21

/* USB standard request codes */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09

/* USB HID class request codes */
#define HID_REQ_GET_REPORT   0x01
#define HID_REQ_SET_REPORT   0x09
#define HID_REQ_GET_IDLE     0x02
#define HID_REQ_SET_IDLE     0x0A
#define HID_REQ_GET_PROTOCOL 0x03
#define HID_REQ_SET_PROTOCOL 0x0B

/* bmRequestType direction/type/recipient */
#define USB_DIR_OUT  0x00
#define USB_DIR_IN   0x80
#define USB_TYPE_STD 0x00
#define USB_TYPE_CLASS 0x20
#define USB_RECIP_DEV   0x00
#define USB_RECIP_IFACE 0x01
#define USB_RECIP_EP    0x02

/* Endpoint attributes */
#define USB_EP_ATTR_CTRL  0x00
#define USB_EP_ATTR_ISO   0x01
#define USB_EP_ATTR_BULK  0x02
#define USB_EP_ATTR_INT   0x03

/* USB Setup Packet (8 bytes, always little-endian) */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

/* USB Device Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_dev_desc_t;

/* USB Configuration Descriptor (first 9 bytes) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  MaxPower;
} __attribute__((packed)) usb_cfg_desc_t;

/* USB Interface Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_iface_desc_t;

/* USB Endpoint Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;  /* bit7=1 → IN */
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_ep_desc_t;

#endif
