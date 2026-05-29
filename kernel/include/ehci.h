#ifndef SIMPLE_EHCI_H
#define SIMPLE_EHCI_H

#include "types.h"

/* ── EHCI capability register offsets (from BAR0) ── */
#define EHCI_CAP_CAPLENGTH   0x00   /* 1 byte: length of capability regs */
#define EHCI_CAP_HCIVERSION  0x02   /* 2 bytes: interface version */
#define EHCI_CAP_HCSPARAMS   0x04   /* structural params */
#define EHCI_CAP_HCCPARAMS   0x08   /* capability params */

/* ── EHCI operational register offsets (from BAR0 + CAPLENGTH) ── */
#define EHCI_OP_USBCMD       0x00
#define EHCI_OP_USBSTS       0x04
#define EHCI_OP_USBINTR      0x08
#define EHCI_OP_FRINDEX      0x0C
#define EHCI_OP_CTRLDSSEG    0x10
#define EHCI_OP_PERIODICBASE 0x14
#define EHCI_OP_ASYNCADDR    0x18
#define EHCI_OP_CONFIGFLAG   0x40
#define EHCI_OP_PORTSC(n)    (0x44u + (uint32_t)(n) * 4u)

/* USBCMD bits */
#define USBCMD_RS      (1u <<  0)   /* Run/Stop */
#define USBCMD_RESET   (1u <<  1)   /* HC Reset */
#define USBCMD_FLS_1K  (0u <<  2)   /* Frame List Size = 1024 */
#define USBCMD_PSE     (1u <<  4)   /* Periodic Schedule Enable */
#define USBCMD_ASE     (1u <<  5)   /* Async Schedule Enable */
#define USBCMD_IAAD    (1u <<  6)   /* Interrupt Async Advance Doorbell */
#define USBCMD_ITC(n)  ((n) << 16)  /* Interrupt Threshold Control */

/* USBSTS bits */
#define USBSTS_INT      (1u <<  0)
#define USBSTS_ERRINT   (1u <<  1)
#define USBSTS_PCD      (1u <<  2)   /* Port Change Detect */
#define USBSTS_FLR      (1u <<  3)
#define USBSTS_HSE      (1u <<  4)
#define USBSTS_IAA      (1u <<  5)   /* Interrupt on Async Advance */
#define USBSTS_HALTED   (1u << 12)
#define USBSTS_PSS      (1u << 14)   /* Periodic Schedule Status */
#define USBSTS_ASS      (1u << 15)   /* Async Schedule Status */

/* PORTSC bits */
#define PORTSC_CCS      (1u <<  0)   /* Current Connect Status */
#define PORTSC_CSC      (1u <<  1)   /* Connect Status Change (W1C) */
#define PORTSC_PED      (1u <<  2)   /* Port Enabled */
#define PORTSC_PEDC     (1u <<  3)   /* Port Enable/Disable Change (W1C) */
#define PORTSC_OCA      (1u <<  4)
#define PORTSC_OCC      (1u <<  5)
#define PORTSC_FPR      (1u <<  6)
#define PORTSC_SUSP     (1u <<  7)
#define PORTSC_PR       (1u <<  8)   /* Port Reset */
#define PORTSC_LS(p)    (((p) >> 10) & 3u)  /* Line Status */
#define PORTSC_PP       (1u << 12)   /* Port Power */
#define PORTSC_OWNER    (1u << 13)   /* Port Owner (1=companion) */
#define PORTSC_W1C_MASK (PORTSC_CSC | PORTSC_PEDC | PORTSC_OCC)

/* ── qTD (Queue Transfer Descriptor) - 32 bytes, 32-byte aligned ── */
typedef struct {
    volatile uint32_t next;      /* Next qTD pointer (bit0 = T terminate) */
    volatile uint32_t alt_next;  /* Alternate qTD pointer */
    volatile uint32_t token;     /* Transfer token */
    volatile uint32_t buf[5];    /* Buffer page pointers (4K pages) */
} __attribute__((packed)) ehci_qtd_t;

/* qTD token bits */
#define QTD_PING      (1u <<  0)
#define QTD_SPLITX    (1u <<  1)
#define QTD_MMF       (1u <<  2)
#define QTD_XACTERR   (1u <<  3)
#define QTD_BABBLE    (1u <<  4)
#define QTD_DBERR     (1u <<  5)
#define QTD_HALTED    (1u <<  6)
#define QTD_ACTIVE    (1u <<  7)
#define QTD_PID_OUT   (0u <<  8)
#define QTD_PID_IN    (1u <<  8)
#define QTD_PID_SETUP (2u <<  8)
#define QTD_CERR(n)   ((uint32_t)(n) << 10)
#define QTD_IOC       (1u << 15)
#define QTD_BYTES(n)  ((uint32_t)(n) << 16)
#define QTD_TOGGLE    (1u << 31)
#define QTD_TERM      1u            /* Terminate bit */

/* Error status mask (excludes ACTIVE and HALTED) */
#define QTD_ERR_MASK  (QTD_XACTERR | QTD_BABBLE | QTD_DBERR)

/* ── Queue Head - 48 bytes, 32-byte aligned ── */
typedef struct {
    volatile uint32_t hlp;       /* Horizontal Link Pointer */
    volatile uint32_t ep_char;   /* Endpoint Characteristics */
    volatile uint32_t ep_cap;    /* Endpoint Capabilities */
    volatile uint32_t curr_qtd;  /* Current qTD Pointer */
    /* Hardware-managed overlay (mirrors qTD) */
    volatile uint32_t ov_next;
    volatile uint32_t ov_alt;
    volatile uint32_t ov_token;
    volatile uint32_t ov_buf[5];
} __attribute__((packed)) ehci_qh_t;

/* QH ep_char bits */
#define QH_DEVADDR(a)  ((uint32_t)(a) & 0x7Fu)
#define QH_INACT       (1u <<  7)
#define QH_EP(n)       ((uint32_t)(n) << 8)
#define QH_EPS_FULL    (1u << 12)   /* Full speed */
#define QH_EPS_LOW     (2u << 12)   /* Low speed */
#define QH_EPS_HIGH    (2u << 12)   /* High speed */
#define QH_DTC         (1u << 14)   /* Data Toggle Control */
#define QH_RECLH       (1u << 15)   /* Head of Reclamation List */
#define QH_MAXPKT(n)   ((uint32_t)(n) << 16)
#define QH_CTRL_EP     (1u << 27)   /* Control endpoint (FS/LS) */
#define QH_RL(n)       ((uint32_t)(n) << 28)

/* QH ep_cap bits */
#define QH_SMASK(m)    ((uint32_t)(m) & 0xFFu)
#define QH_CMASK(m)    (((uint32_t)(m) & 0xFFu) << 8)
#define QH_HUBADDR(a)  (((uint32_t)(a) & 0x7Fu) << 16)
#define QH_HUBPORT(p)  (((uint32_t)(p) & 0x7Fu) << 23)

/* HLP / qTD pointer type field */
#define HLP_TYPE_ITD   (0u << 1)
#define HLP_TYPE_QH    (1u << 1)
#define HLP_TYPE_SITD  (2u << 1)
#define HLP_TYPE_FSTN  (3u << 1)

/* USB device speeds */
#define USB_SPEED_FULL 0
#define USB_SPEED_LOW  1
#define USB_SPEED_HIGH 2

/* ── Per-device HID slot ── */
#define EHCI_MAX_HID  8

typedef struct {
    int      active;
    uint8_t  ctrl_idx;      /* Which EHCI controller (0 or 1) */
    uint8_t  port;          /* Root hub port (0-based) */
    uint8_t  dev_addr;
    uint8_t  speed;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t  hid_type;      /* 1=keyboard 2=mouse */
    uint8_t  hid_iface;
    uint8_t  hid_ep;        /* interrupt IN endpoint address */
    uint16_t hid_mps;       /* endpoint max packet size */
    uint8_t  ep0_mps;
    uint8_t  hub_addr;      /* TT hub address (for FS/LS) */
    uint8_t  hub_port;      /* TT hub port (for FS/LS) */
    /* Periodic transfer state */
    ehci_qh_t  *qh;
    ehci_qtd_t *qtd;
    uint8_t    *buf;
    uint8_t     prev_report[8]; /* previous keyboard/mouse report */
} ehci_hid_t;

#define HID_TYPE_KBD   1
#define HID_TYPE_MOUSE 2

/* ── Public interface ── */

/* Initialise all detected EHCI controllers, enumerate ports, set up HID. */
void usb_init(void);

/* Called from pit_timer_tick on every PIT tick.
 * Checks HID periodic qTDs for completed reports and handles hotplug. */
void usb_poll(void);

/* Status accessors for the "usb" shell command. */
int  usb_get_ctrl_count(void);
int  usb_get_hid_count(void);
const ehci_hid_t *usb_get_hid(int idx);

/* Returns a static string describing the EHCI controller. */
const char *usb_ctrl_info(int idx, char *buf, int bufsz);

#endif
