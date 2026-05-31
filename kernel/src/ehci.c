/*
 * ehci.c — EHCI USB 2.0 host controller driver.
 *
 * Supports Intel H87 PCH (and compatible EHCI-2.0 controllers).
 * Uses a polling model: usb_poll() is called from pit_timer_tick() every
 * 10 ms.  No EHCI interrupt line is required.
 *
 * Transfer strategy:
 *   Enumeration (init time): async schedule, synchronous polling.
 *   HID input (runtime):     periodic schedule, polled from PIT handler.
 */

#include "ehci.h"
#include "pci.h"
#include "paging.h"
#include "usb.h"
#include "usb_hid.h"
#include "pit.h"
#include "serial.h"
#include "string.h"

/* ── PCI class for EHCI ── */
#define PCI_CLASS_SERIAL   0x0C
#define PCI_SUB_USB        0x03
#define PCI_PROG_EHCI      0x20

#define MAX_EHCI_CTRL      2
#define MAX_ROOT_PORTS     8
#define CTRL_TIMEOUT_TICKS 20   /* 200 ms at 100 Hz */
#define ENUM_TIMEOUT_TICKS 10   /* 100 ms */

/* ── Per-controller state ── */
typedef struct {
    int      valid;
    uint8_t  pci_bus, pci_dev, pci_fn;
    uint32_t bar;          /* MMIO base (BAR0) */
    uint32_t op_base;      /* bar + CAPLENGTH */
    uint8_t  nports;
    /* Ports: last-known CCS per port */
    uint8_t  port_ccs[MAX_ROOT_PORTS];
} ehci_ctrl_t;

static ehci_ctrl_t g_ctrl[MAX_EHCI_CTRL];
static int         g_nctrl = 0;

/* ── Static DMA-safe structures (identity-mapped kernel BSS) ── */

/* Periodic frame lists (4 KB each, 4 KB aligned) */
static uint32_t g_frame_list[MAX_EHCI_CTRL][1024]
    __attribute__((aligned(4096)));

/* Async schedule dummy head QH (one per controller) */
static ehci_qh_t g_async_head[MAX_EHCI_CTRL]
    __attribute__((aligned(32)));

/* Re-usable QH + qTDs for synchronous control transfers */
static ehci_qh_t  g_ctrl_qh [MAX_EHCI_CTRL]
    __attribute__((aligned(32)));
static ehci_qtd_t g_ctrl_qtd[MAX_EHCI_CTRL][3]   /* setup / data / status */
    __attribute__((aligned(32)));
static uint8_t    g_ctrl_buf[MAX_EHCI_CTRL][256]; /* setup pkt + descriptor scratch */

/* Per-HID-device periodic QH, qTD, and report buffer */
static ehci_qh_t  g_hid_qh [EHCI_MAX_HID]
    __attribute__((aligned(32)));
static ehci_qtd_t g_hid_qtd[EHCI_MAX_HID]
    __attribute__((aligned(32)));
static uint8_t    g_hid_buf [EHCI_MAX_HID][8];

/* HID device table */
static ehci_hid_t g_hid[EHCI_MAX_HID];
static int        g_nhid = 0;

/* Next USB device address to assign (1-based) */
static uint8_t g_next_addr = 1;

/* Set to 1 once usb_init() completes; usb_poll() is a no-op before that */
static volatile int g_ready = 0;

/* ── Register accessors ── */

static inline uint32_t cap_read(uint32_t bar, uint32_t off) {
    return *(volatile uint32_t *)(bar + off);
}

static inline uint32_t op_read(uint32_t op, uint32_t off) {
    return *(volatile uint32_t *)(op + off);
}

static inline void op_write(uint32_t op, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(op + off) = val;
}

/* ── Busy-wait helpers (loop-count based, safe inside IRQ handler) ── */

static void udelay(uint32_t us) {
    /* NOP-loop busy wait. Approximate — only used for sub-ms pauses
     * inside wait_bits() and small spec delays (≤ 1 ms).
     * For delays ≥ 10 ms always use mdelay(). */
    volatile uint32_t n = us * 100u;
    while (n--) __asm__ volatile("nop");
}

/* PIT-based millisecond delay. 100 Hz PIT → 1 tick = 10 ms.
 * Accurate on real hardware regardless of CPU speed.
 * Always PIT-based; minimum wait is one full tick (10 ms) so even
 * mdelay(1) and mdelay(2) give the USB device adequate recovery time. */
static void mdelay(uint32_t ms) {
    uint32_t ticks = (ms + 9) / 10;           /* round up: 100 Hz → 10 ms/tick */
    if (ticks == 0) ticks = 1;                /* minimum 1 tick = 10 ms */
    uint32_t end   = pit_ticks() + ticks + 1; /* +1: guard against tick boundary */
    while ((int32_t)(pit_ticks() - end) < 0)
        __asm__ volatile("pause");
}

/* Spin until (reg & mask) == val, with loop-count timeout.
 * Returns 1 on success, 0 on timeout. */
static int wait_bits(uint32_t op, uint32_t off, uint32_t mask,
                     uint32_t val, uint32_t loops)
{
    while (loops--) {
        if ((op_read(op, off) & mask) == val) return 1;
        udelay(10);
    }
    return 0;
}

/* ── EHCI BIOS ownership handoff ── */

static void ehci_bios_handoff(uint8_t bus, uint8_t dev, uint8_t fn,
                              uint32_t bar)
{
    uint32_t hccparams = cap_read(bar, EHCI_CAP_HCCPARAMS);
    uint8_t  eecp = (uint8_t)((hccparams >> 8) & 0xFF);

    if (!eecp) return;

    uint32_t legsup = pci_read32(bus, dev, fn, eecp);
    /* Check EHCI Extended Capability ID = 1 (Legacy Support) */
    if ((legsup & 0xFF) != 1) return;

    if (legsup & (1u << 16)) {
        /* BIOS owns — request OS ownership */
        pci_write32(bus, dev, fn, eecp, legsup | (1u << 24));
        /* Wait up to 2 s for BIOS to release */
        for (uint32_t i = 0; i < 2000; i++) {
            legsup = pci_read32(bus, dev, fn, eecp);
            if (!(legsup & (1u << 16))) break;
            mdelay(1);
        }
    }
    /* Disable SMI generation from BIOS */
    uint32_t legctlsts = pci_read32(bus, dev, fn, (uint8_t)(eecp + 4));
    legctlsts &= ~0x1F;     /* clear BIOS SMI enables */
    pci_write32(bus, dev, fn, (uint8_t)(eecp + 4), legctlsts);

    serial_write(COM1, "[EHCI] BIOS handoff complete\n");
}

/* ── Async schedule init ── */

static void ehci_async_init(int ci)
{
    uint32_t op = g_ctrl[ci].op_base;

    /* Dummy head QH: circular, H=1, no active qTDs */
    ehci_qh_t *head = &g_async_head[ci];
    uint32_t   self = (uint32_t)head;

    head->hlp      = (self & ~0x1Fu) | HLP_TYPE_QH;  /* points to itself */
    head->ep_char  = QH_RECLH;
    head->ep_cap   = 0;
    head->curr_qtd = 0;
    head->ov_next  = QTD_TERM;
    head->ov_alt   = QTD_TERM;
    head->ov_token = 0;
    for (int i = 0; i < 5; i++) head->ov_buf[i] = 0;

    op_write(op, EHCI_OP_ASYNCADDR, self & ~0x1Fu);
}

/* ── Periodic schedule init ── */

static void ehci_periodic_init(int ci)
{
    uint32_t op = g_ctrl[ci].op_base;

    /* All frames: terminate */
    for (int i = 0; i < 1024; i++)
        g_frame_list[ci][i] = QTD_TERM;

    op_write(op, EHCI_OP_PERIODICBASE, (uint32_t)g_frame_list[ci]);
}

/* ── Controller reset and start ── */

static int ehci_reset_ctrl(int ci)
{
    uint32_t op = g_ctrl[ci].op_base;

    /* Stop the controller first */
    uint32_t cmd = op_read(op, EHCI_OP_USBCMD);
    op_write(op, EHCI_OP_USBCMD, cmd & ~USBCMD_RS);
    if (!wait_bits(op, EHCI_OP_USBSTS, USBSTS_HALTED, USBSTS_HALTED, 500)) {
        serial_write(COM1, "[EHCI] stop timeout\n");
        return 0;
    }

    /* Reset */
    op_write(op, EHCI_OP_USBCMD, USBCMD_RESET);
    if (!wait_bits(op, EHCI_OP_USBCMD, USBCMD_RESET, 0, 1000)) {
        serial_write(COM1, "[EHCI] reset timeout\n");
        return 0;
    }

    /* Clear segment register (must be 0 for 32-bit addressing) */
    op_write(op, EHCI_OP_CTRLDSSEG, 0);

    /* Set up schedules */
    ehci_async_init(ci);
    ehci_periodic_init(ci);

    /* ITC=1 microframe interrupt threshold; disable all EHCI interrupts
     * (we poll, no IRQ needed). */
    op_write(op, EHCI_OP_USBINTR,  0);
    op_write(op, EHCI_OP_USBCMD, USBCMD_RS | USBCMD_ITC(1));

    if (!wait_bits(op, EHCI_OP_USBSTS, USBSTS_HALTED, 0, 500)) {
        serial_write(COM1, "[EHCI] run timeout\n");
        return 0;
    }

    /* Route all ports to EHCI (not companion controllers) */
    op_write(op, EHCI_OP_CONFIGFLAG, 1);
    udelay(100);

    return 1;
}

/* ── Build a qTD ── */

static void qtd_init(ehci_qtd_t *qtd, uint32_t next, uint32_t token,
                     uint32_t buf, uint32_t len)
{
    qtd->next     = next;
    qtd->alt_next = QTD_TERM;
    qtd->token    = token;
    qtd->buf[0]   = buf;
    /* Cross-page buffer pointer (only needed if buf crosses a 4 K boundary) */
    qtd->buf[1]   = len ? ((buf + 4095u) & ~4095u) : 0;
    qtd->buf[2]   = 0;
    qtd->buf[3]   = 0;
    qtd->buf[4]   = 0;
}

/* ── Synchronous control transfer via async schedule ── */
/*
 * Performs a full USB control transfer (Setup + Data + Status).
 * Called only from usb_init() context (interrupts enabled but usb_poll
 * returns early until g_ready=1, so no async hazard).
 */
static int ehci_control(int ci, uint8_t dev_addr, uint8_t speed,
                        uint8_t ep0_mps, uint8_t hub_addr, uint8_t hub_port,
                        const usb_setup_t *setup, void *data, uint16_t len)
{
    uint32_t op   = g_ctrl[ci].op_base;
    int      is_in = (setup->bmRequestType & USB_DIR_IN) != 0;

    ehci_qh_t  *qh  = &g_ctrl_qh[ci];
    ehci_qtd_t *sq  = &g_ctrl_qtd[ci][0];  /* SETUP  */
    ehci_qtd_t *dq  = &g_ctrl_qtd[ci][1];  /* DATA   */
    ehci_qtd_t *stq = &g_ctrl_qtd[ci][2];  /* STATUS */

    /* Copy setup packet to DMA-accessible static buffer */
    uint8_t *sp = g_ctrl_buf[ci];
    sp[0] = setup->bmRequestType;
    sp[1] = setup->bRequest;
    sp[2] = (uint8_t)(setup->wValue);
    sp[3] = (uint8_t)(setup->wValue >> 8);
    sp[4] = (uint8_t)(setup->wIndex);
    sp[5] = (uint8_t)(setup->wIndex >> 8);
    sp[6] = (uint8_t)(setup->wLength);
    sp[7] = (uint8_t)(setup->wLength >> 8);

    /* STATUS qTD (always opposite direction of DATA, DATA1 toggle) */
    uint32_t stat_pid = is_in ? QTD_PID_OUT : QTD_PID_IN;
    qtd_init(stq, QTD_TERM,
             QTD_ACTIVE | QTD_CERR(3) | stat_pid | QTD_TOGGLE | QTD_IOC,
             0, 0);

    /* DATA qTD (if transfer has a data phase) */
    if (len && data) {
        uint32_t data_pid = is_in ? QTD_PID_IN : QTD_PID_OUT;
        qtd_init(dq, (uint32_t)stq,
                 QTD_ACTIVE | QTD_CERR(3) | data_pid | QTD_BYTES(len) | QTD_TOGGLE,
                 (uint32_t)data, len);
    }

    /* SETUP qTD (DATA0 toggle, always OUT regardless of transfer direction) */
    uint32_t data_next = (len && data) ? (uint32_t)dq : (uint32_t)stq;
    qtd_init(sq, data_next,
             QTD_ACTIVE | QTD_CERR(3) | QTD_PID_SETUP | QTD_BYTES(8),
             (uint32_t)sp, 8);

    /* Configure QH */
    uint32_t eps_bits = (speed == USB_SPEED_HIGH) ? QH_EPS_HIGH :
                        (speed == USB_SPEED_LOW)  ? QH_EPS_LOW  : QH_EPS_FULL;
    int fs_ls = (speed != USB_SPEED_HIGH);

    qh->ep_char = QH_DEVADDR(dev_addr)
                | QH_EP(0)
                | eps_bits
                | QH_DTC
                | QH_MAXPKT(ep0_mps)
                | (fs_ls ? QH_CTRL_EP : 0);

    qh->ep_cap  = fs_ls ? (QH_HUBADDR(hub_addr) | QH_HUBPORT(hub_port)) : 0;

    /* Reset the QH overlay to point at the SETUP qTD */
    qh->curr_qtd = 0;
    qh->ov_next  = (uint32_t)sq;
    qh->ov_alt   = QTD_TERM;
    qh->ov_token = 0;
    for (int i = 0; i < 5; i++) qh->ov_buf[i] = 0;

    /* Insert QH after the dummy head (circular list: head → qh → head) */
    ehci_qh_t *head = &g_async_head[ci];
    qh->hlp  = head->hlp;  /* qh.next = head (was self-loop) */
    head->hlp = ((uint32_t)qh & ~0x1Fu) | HLP_TYPE_QH;

    /* Enable async schedule */
    uint32_t cmd = op_read(op, EHCI_OP_USBCMD);
    if (!(cmd & USBCMD_ASE)) {
        op_write(op, EHCI_OP_USBCMD, cmd | USBCMD_ASE);
        wait_bits(op, EHCI_OP_USBSTS, USBSTS_ASS, USBSTS_ASS, 500);
    }

    /* Poll STATUS qTD until not ACTIVE (or timeout) */
    int result = -1;
    uint32_t deadline = pit_ticks() + ENUM_TIMEOUT_TICKS;
    while (pit_ticks() < deadline) {
        uint32_t tok = stq->token;
        if (!(tok & QTD_ACTIVE)) {
            result = (tok & (QTD_ERR_MASK | QTD_HALTED)) ? -1 : 0;
            break;
        }
        udelay(500);
    }

    /* Remove QH from async list */
    head->hlp = qh->hlp;

    /* Ring the async advance doorbell and wait for IAA */
    cmd = op_read(op, EHCI_OP_USBCMD);
    op_write(op, EHCI_OP_USBCMD, cmd | USBCMD_IAAD);
    for (uint32_t i = 0; i < 1000; i++) {
        if (op_read(op, EHCI_OP_USBSTS) & USBSTS_IAA) break;
        udelay(10);
    }
    op_write(op, EHCI_OP_USBSTS, USBSTS_IAA); /* W1C */

    return result;
}

/* ── USB standard control helpers ── */

static int usb_get_descriptor(int ci, uint8_t addr, uint8_t speed,
                              uint8_t ep0_mps, uint8_t hub_addr,
                              uint8_t hub_port, uint8_t dt, uint8_t idx,
                              void *buf, uint16_t len)
{
    usb_setup_t s = {
        .bmRequestType = USB_DIR_IN | USB_TYPE_STD | USB_RECIP_DEV,
        .bRequest      = USB_REQ_GET_DESCRIPTOR,
        .wValue        = (uint16_t)((dt << 8) | idx),
        .wIndex        = 0,
        .wLength       = len
    };
    return ehci_control(ci, addr, speed, ep0_mps, hub_addr, hub_port, &s, buf, len);
}

static int usb_set_address(int ci, uint8_t new_addr, uint8_t speed,
                           uint8_t hub_addr, uint8_t hub_port)
{
    usb_setup_t s = {
        .bmRequestType = USB_DIR_OUT | USB_TYPE_STD | USB_RECIP_DEV,
        .bRequest      = USB_REQ_SET_ADDRESS,
        .wValue        = new_addr,
        .wIndex        = 0,
        .wLength       = 0
    };
    return ehci_control(ci, 0, speed, 8, hub_addr, hub_port, &s, NULL, 0);
}

static int usb_set_configuration(int ci, uint8_t addr, uint8_t speed,
                                 uint8_t ep0_mps, uint8_t hub_addr,
                                 uint8_t hub_port, uint8_t cfg)
{
    usb_setup_t s = {
        .bmRequestType = USB_DIR_OUT | USB_TYPE_STD | USB_RECIP_DEV,
        .bRequest      = USB_REQ_SET_CONFIGURATION,
        .wValue        = cfg,
        .wIndex        = 0,
        .wLength       = 0
    };
    return ehci_control(ci, addr, speed, ep0_mps, hub_addr, hub_port, &s, NULL, 0);
}

static int hid_set_protocol(int ci, uint8_t addr, uint8_t speed,
                            uint8_t ep0_mps, uint8_t hub_addr,
                            uint8_t hub_port, uint8_t iface, uint8_t protocol)
{
    usb_setup_t s = {
        .bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_IFACE,
        .bRequest      = HID_REQ_SET_PROTOCOL,
        .wValue        = protocol,   /* 0 = boot, 1 = report */
        .wIndex        = iface,
        .wLength       = 0
    };
    return ehci_control(ci, addr, speed, ep0_mps, hub_addr, hub_port, &s, NULL, 0);
}

static int hid_set_idle(int ci, uint8_t addr, uint8_t speed,
                        uint8_t ep0_mps, uint8_t hub_addr,
                        uint8_t hub_port, uint8_t iface)
{
    /* duration=0: send reports only on change (no idle repeat) */
    usb_setup_t s = {
        .bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_IFACE,
        .bRequest      = HID_REQ_SET_IDLE,
        .wValue        = 0,
        .wIndex        = iface,
        .wLength       = 0
    };
    return ehci_control(ci, addr, speed, ep0_mps, hub_addr, hub_port, &s, NULL, 0);
}

/* ── Port reset ── */

static int ehci_port_reset(int ci, int port)
{
    uint32_t op = g_ctrl[ci].op_base;
    uint32_t portsc = op_read(op, EHCI_OP_PORTSC(port));

    if (!(portsc & PORTSC_CCS)) return -1;  /* nothing connected */

    /* Power the port if needed */
    if (!(portsc & PORTSC_PP)) {
        op_write(op, EHCI_OP_PORTSC(port), portsc | PORTSC_PP);
        mdelay(20);
        portsc = op_read(op, EHCI_OP_PORTSC(port));
    }

    /* Assert reset for ≥50 ms (EHCI spec 4.2.2) */
    portsc &= ~(PORTSC_W1C_MASK);   /* preserve, don't W1C accidentally */
    op_write(op, EHCI_OP_PORTSC(port), portsc | PORTSC_PR);
    mdelay(60);

    /* Clear PR */
    portsc = op_read(op, EHCI_OP_PORTSC(port));
    op_write(op, EHCI_OP_PORTSC(port), portsc & ~PORTSC_PR);

    /* Wait for reset to complete */
    mdelay(2);

    portsc = op_read(op, EHCI_OP_PORTSC(port));

    if (portsc & PORTSC_PED) {
        /* High-speed device: port is enabled */
        serial_write(COM1, "[EHCI] port ");
        serial_write_dec(COM1, (uint32_t)port + 1);
        serial_write(COM1, " HS enabled\n");
        return USB_SPEED_HIGH;
    }

    /* FS/LS device: port not auto-enabled by EHCI.
     * On Intel PCH with integrated TT we can force-enable it. */
    uint32_t ls = PORTSC_LS(portsc);
    int speed = (ls == 1) ? USB_SPEED_LOW : USB_SPEED_FULL;

    /* Force-enable the port (Intel ICH/PCH specific; enables TT path) */
    portsc = op_read(op, EHCI_OP_PORTSC(port));
    op_write(op, EHCI_OP_PORTSC(port), portsc | PORTSC_PED);
    mdelay(2);
    portsc = op_read(op, EHCI_OP_PORTSC(port));

    serial_write(COM1, "[EHCI] port ");
    serial_write_dec(COM1, (uint32_t)port + 1);
    serial_write(COM1, speed == USB_SPEED_LOW ? " LS" : " FS");
    serial_write(COM1, " enabled\n");

    return speed;
}

/* ── Parse configuration descriptor for HID interface ── */

static int parse_config(const uint8_t *buf, int total,
                        uint8_t *out_iface, uint8_t *out_hid_type,
                        uint8_t *out_ep, uint16_t *out_mps,
                        uint8_t *out_interval)
{
    int i = 0;
    uint8_t cur_iface = 0;
    int in_hid = 0;
    *out_iface    = 0;
    *out_hid_type = 0;
    *out_ep       = 0;
    *out_mps      = 8;
    *out_interval = 10;

    while (i < total) {
        uint8_t bLen  = buf[i];
        uint8_t bType = buf[i + 1];

        if (!bLen || i + bLen > total) break;

        if (bType == USB_DT_INTERFACE) {
            usb_iface_desc_t *id = (usb_iface_desc_t *)(buf + i);
            cur_iface = id->bInterfaceNumber;
            /* HID boot class: class=3, subclass=1, protocol=1(kbd)/2(mouse) */
            if (id->bInterfaceClass == 3 && id->bInterfaceSubClass == 1) {
                in_hid = 1;
                *out_iface    = cur_iface;
                *out_hid_type = id->bInterfaceProtocol; /* 1=kbd 2=mouse */
            } else {
                in_hid = 0;
            }
        } else if (bType == USB_DT_ENDPOINT && in_hid) {
            usb_ep_desc_t *ep = (usb_ep_desc_t *)(buf + i);
            /* Looking for interrupt IN endpoint */
            if ((ep->bEndpointAddress & 0x80) &&
                (ep->bmAttributes & 0x03) == USB_EP_ATTR_INT)
            {
                *out_ep       = ep->bEndpointAddress;
                *out_mps      = ep->wMaxPacketSize;
                *out_interval = ep->bInterval;
                return 1;
            }
        }
        i += bLen;
    }
    return 0;
}

/* ── Arm a periodic interrupt qTD for a HID device ── */

static void hid_arm_qtd(int slot)
{
    ehci_hid_t *h   = &g_hid[slot];
    ehci_qtd_t *qtd = &g_hid_qtd[slot];
    uint8_t    *buf = g_hid_buf[slot];
    uint16_t    mps = h->hid_mps > 8 ? 8 : h->hid_mps;

    qtd->next     = QTD_TERM;
    qtd->alt_next = QTD_TERM;
    /* No QTD_TOGGLE: DTC=0 in QH so EHCI manages the DATA0/DATA1 sequence.
     * Forcing DATA1 here caused QEMU to see a toggle mismatch on the first
     * transfer (device sends DATA0) and return USB_RET_IOERROR → HC reset. */
    qtd->token    = QTD_ACTIVE | QTD_CERR(3) | QTD_PID_IN
                  | QTD_BYTES(mps) | QTD_IOC;
    qtd->buf[0]   = (uint32_t)buf;
    /* Next 4 K page pointer — required if buf straddles a page boundary */
    qtd->buf[1]   = ((uint32_t)buf + 4095u) & ~4095u;
    qtd->buf[2]   = 0;
    qtd->buf[3]   = 0;
    qtd->buf[4]   = 0;

    /* Reload the QH overlay.
     * Preserve the toggle bit so EHCI continues the correct DATA0/DATA1
     * sequence after a re-arm.  On the very first arm the overlay comes
     * from zero-initialised BSS so ov_token=0 → DATA0 (correct). */
    ehci_qh_t *qh  = &g_hid_qh[slot];
    uint32_t toggle = qh->ov_token & QTD_TOGGLE;
    qh->ov_next   = (uint32_t)qtd;
    qh->ov_alt    = QTD_TERM;
    qh->ov_token  = toggle;   /* clear HALTED/errors, keep DATA toggle */
}

/* ── Add a HID device to the periodic schedule ── */

static int hid_add_periodic(int ci, int slot)
{
    ehci_hid_t *h  = &g_hid[slot];
    ehci_qh_t  *qh = &g_hid_qh[slot];

    int fs_ls   = (h->speed != USB_SPEED_HIGH);
    uint8_t ep_num = h->hid_ep & 0x0Fu;

    uint32_t eps_bits = (h->speed == USB_SPEED_HIGH) ? QH_EPS_HIGH :
                        (h->speed == USB_SPEED_LOW)  ? QH_EPS_LOW  : QH_EPS_FULL;

    /* For periodic, S-mask=0x01, C-mask=0x1C covers FS split completion */
    uint32_t smask = fs_ls ? 0x01u : 0x01u;
    uint32_t cmask = fs_ls ? 0x1Cu : 0x00u;

    qh->hlp      = QTD_TERM;   /* last in chain; will be set when inserted */
    /* No QH_DTC: let EHCI manage the data toggle automatically.
     * With DTC=1 the toggle came from the qTD (always DATA1), which caused
     * a mismatch against the device's initial DATA0 → "processing error". */
    qh->ep_char  = QH_DEVADDR(h->dev_addr)
                 | QH_EP(ep_num)
                 | eps_bits
                 | QH_MAXPKT(h->hid_mps > 64 ? 64 : h->hid_mps);
    /* QH_MULT(1): EHCI spec Table 3-15 bits 31:30.
     * Mult=0 means "HC shall not execute transactions" — this was the
     * reason periodic transfers never ran before this fix was added. */
    qh->ep_cap   = QH_SMASK(smask)
                 | QH_CMASK(cmask)
                 | (fs_ls ? (QH_HUBADDR(h->hub_addr) | QH_HUBPORT(h->hub_port)) : 0)
                 | QH_MULT(1);
    qh->curr_qtd = 0;

    serial_write(COM1, "[EHCI] HID QH  phys=0x");
    serial_write_hex(COM1, (uint32_t)qh);
    serial_write(COM1, " sizeof_qh=");
    serial_write_dec(COM1, (uint32_t)sizeof(ehci_qh_t));
    serial_write(COM1, "\n");
    serial_write(COM1, "[EHCI]   ep_char=0x");
    serial_write_hex(COM1, qh->ep_char);
    serial_write(COM1, " ep_cap=0x");
    serial_write_hex(COM1, QH_SMASK(smask) | QH_CMASK(cmask)
                           | (fs_ls ? (QH_HUBADDR(h->hub_addr)|QH_HUBPORT(h->hub_port)) : 0)
                           | QH_MULT(1));
    serial_write(COM1, " ep=");
    serial_write_dec(COM1, ep_num);
    serial_write(COM1, " mps=");
    serial_write_dec(COM1, h->hid_mps > 64 ? 64 : h->hid_mps);
    serial_write(COM1, " spd=");
    serial_write_dec(COM1, h->speed);
    serial_write(COM1, "\n");
    serial_write(COM1, "[EHCI] HID qTD phys=0x");
    serial_write_hex(COM1, (uint32_t)&g_hid_qtd[slot]);
    serial_write(COM1, " buf=0x");
    serial_write_hex(COM1, (uint32_t)g_hid_buf[slot]);
    serial_write(COM1, "\n");

    hid_arm_qtd(slot);

    /* Add QH to every frame in the periodic list at this controller */
    uint32_t qh_ptr = ((uint32_t)qh & ~0x1Fu) | HLP_TYPE_QH;
    qh->hlp = g_frame_list[ci][0]; /* chain to whatever was first */
    for (int f = 0; f < 1024; f++)
        g_frame_list[ci][f] = qh_ptr;

    /* Enable periodic schedule */
    uint32_t op  = g_ctrl[ci].op_base;
    uint32_t cmd = op_read(op, EHCI_OP_USBCMD);
    if (!(cmd & USBCMD_PSE)) {
        op_write(op, EHCI_OP_USBCMD, cmd | USBCMD_PSE);
        int pss_ok = wait_bits(op, EHCI_OP_USBSTS, USBSTS_PSS, USBSTS_PSS, 500);
        serial_write(COM1, "[EHCI] PSE enabled PSS=");
        serial_write(COM1, pss_ok ? "1 (running)\n" : "0 (TIMEOUT)\n");
    } else {
        serial_write(COM1, "[EHCI] PSE already set\n");
    }

    serial_write(COM1, "[EHCI] after hid_add_periodic: USBCMD=0x");
    serial_write_hex(COM1, op_read(op, EHCI_OP_USBCMD));
    serial_write(COM1, " USBSTS=0x");
    serial_write_hex(COM1, op_read(op, EHCI_OP_USBSTS));
    serial_write(COM1, "\n");
    return 1;
}

/* ── Enumerate one USB device ── */

static void enumerate_device(int ci, int port, int speed)
{
    if (g_nhid >= EHCI_MAX_HID) return;

    /* Port 0-based, but TT hub_port is 1-based in EHCI spec */
    uint8_t hub_addr = 0;
    uint8_t hub_port = (uint8_t)(port + 1);
    uint8_t new_addr = g_next_addr;

    /* Step 1: GET_DESCRIPTOR(DEVICE, 8 bytes) at address 0, ep0_mps=8 */
    usb_dev_desc_t *dd = (usb_dev_desc_t *)(g_ctrl_buf[ci] + 128);
    if (usb_get_descriptor(ci, 0, (uint8_t)speed, 8, hub_addr, hub_port,
                           USB_DT_DEVICE, 0, dd, 8) < 0) {
        serial_write(COM1, "[USB] GET_DESC(8) failed\n");
        return;
    }
    uint8_t ep0_mps = dd->bMaxPacketSize0;
    if (!ep0_mps) ep0_mps = 8;

    mdelay(2);

    /* Step 2: SET_ADDRESS */
    if (usb_set_address(ci, new_addr, (uint8_t)speed, hub_addr, hub_port) < 0) {
        serial_write(COM1, "[USB] SET_ADDRESS failed\n");
        return;
    }
    mdelay(2);
    g_next_addr++;

    /* Step 3: GET_DESCRIPTOR(DEVICE, full) */
    if (usb_get_descriptor(ci, new_addr, (uint8_t)speed, ep0_mps,
                           hub_addr, hub_port,
                           USB_DT_DEVICE, 0, dd, sizeof(usb_dev_desc_t)) < 0) {
        serial_write(COM1, "[USB] GET_DESC(full) failed\n");
        return;
    }

    serial_write(COM1, "[USB] VID=");
    serial_write_hex(COM1, dd->idVendor);
    serial_write(COM1, " PID=");
    serial_write_hex(COM1, dd->idProduct);
    serial_write(COM1, " class=");
    serial_write_dec(COM1, dd->bDeviceClass);
    serial_write(COM1, "\n");

    /* Step 4: GET_DESCRIPTOR(CONFIGURATION) — fetch full config */
    uint8_t *cfgbuf = g_ctrl_buf[ci];
    if (usb_get_descriptor(ci, new_addr, (uint8_t)speed, ep0_mps,
                           hub_addr, hub_port,
                           USB_DT_CONFIG, 0, cfgbuf, 9) < 0) {
        serial_write(COM1, "[USB] GET_DESC(cfg) failed\n");
        return;
    }
    usb_cfg_desc_t *cd = (usb_cfg_desc_t *)cfgbuf;
    uint16_t total = cd->wTotalLength;
    if (total > 200) total = 200;
    if (usb_get_descriptor(ci, new_addr, (uint8_t)speed, ep0_mps,
                           hub_addr, hub_port,
                           USB_DT_CONFIG, 0, cfgbuf, total) < 0) {
        serial_write(COM1, "[USB] GET_DESC(cfg full) failed\n");
        return;
    }

    /* Step 5: Check for HID boot interface */
    uint8_t  iface, hid_type, hid_ep, hid_interval;
    uint16_t hid_mps;
    if (!parse_config(cfgbuf, (int)total, &iface, &hid_type,
                      &hid_ep, &hid_mps, &hid_interval)) {
        serial_write(COM1, "[USB] not HID boot device\n");
        return;
    }

    /* Step 6: SET_CONFIGURATION */
    if (usb_set_configuration(ci, new_addr, (uint8_t)speed, ep0_mps,
                              hub_addr, hub_port,
                              cd->bConfigurationValue) < 0) {
        serial_write(COM1, "[USB] SET_CONFIG failed\n");
        return;
    }
    mdelay(2);

    /* Step 7: HID SET_PROTOCOL(boot=0) */
    hid_set_protocol(ci, new_addr, (uint8_t)speed, ep0_mps,
                     hub_addr, hub_port, iface, 0);
    mdelay(1);

    /* Step 8: HID SET_IDLE(0) — report only on change */
    hid_set_idle(ci, new_addr, (uint8_t)speed, ep0_mps,
                 hub_addr, hub_port, iface);
    mdelay(1);

    /* Register device */
    int slot = g_nhid;
    ehci_hid_t *h = &g_hid[slot];
    h->active    = 1;
    h->ctrl_idx  = (uint8_t)ci;
    h->port      = (uint8_t)port;
    h->dev_addr  = new_addr;
    h->speed     = (uint8_t)speed;
    h->vendor_id = dd->idVendor;
    h->product_id= dd->idProduct;
    h->hid_type  = hid_type;
    h->hid_iface = iface;
    h->hid_ep    = hid_ep;
    h->hid_mps   = hid_mps;
    h->ep0_mps   = ep0_mps;
    h->hub_addr  = hub_addr;
    h->hub_port  = hub_port;
    h->qh        = &g_hid_qh[slot];
    h->qtd       = &g_hid_qtd[slot];
    h->buf       = g_hid_buf[slot];
    for (int i = 0; i < 8; i++) h->prev_report[i] = 0;

    hid_add_periodic(ci, slot);
    g_nhid++;

    const char *typstr = (hid_type == HID_TYPE_KBD) ? "Keyboard" : "Mouse";
    serial_write(COM1, "[USB] HID ");
    serial_write(COM1, typstr);
    serial_write(COM1, " addr=");
    serial_write_dec(COM1, new_addr);
    serial_write(COM1, " ep=");
    serial_write_hex(COM1, hid_ep);
    serial_write(COM1, "\n");
}

/* ── Scan one controller's root hub ports ── */

static void scan_ports(int ci)
{
    uint32_t op    = g_ctrl[ci].op_base;
    uint8_t nports = g_ctrl[ci].nports;

    for (int p = 0; p < nports; p++) {
        uint32_t portsc = op_read(op, EHCI_OP_PORTSC(p));

        /* Clear any write-1-to-clear status bits that might confuse us */
        uint32_t w1c = portsc & PORTSC_W1C_MASK;
        if (w1c) op_write(op, EHCI_OP_PORTSC(p), (portsc & ~PORTSC_W1C_MASK) | w1c);

        portsc = op_read(op, EHCI_OP_PORTSC(p));
        g_ctrl[ci].port_ccs[p] = (portsc & PORTSC_CCS) ? 1 : 0;

        if (!(portsc & PORTSC_CCS)) continue;

        serial_write(COM1, "[EHCI] port ");
        serial_write_dec(COM1, (uint32_t)p + 1);
        serial_write(COM1, " connected\n");

        int speed = ehci_port_reset(ci, p);
        if (speed < 0) continue;

        mdelay(10);  /* device recovery time */
        enumerate_device(ci, p, speed);
    }
}

/* ── Intel XHCI → EHCI USB 2.0 port routing handoff ── */
/*
 * On Intel 8-series PCH (H81, H87, B85, …) the XHCI controller starts up
 * owning all USB 2.0 ports.  EHCI sees no connected devices unless we
 * release those ports here.  USB2PRM at PCI config offset 0xD4 is a bitmask
 * of ports currently routed to XHCI; writing 0 routes them all to EHCI.
 */
static void intel_xhci_handoff(void)
{
    uint8_t bus, dev, fn;
    if (!pci_find_device(0x0C, 0x03, 0x30, &bus, &dev, &fn)) {
        serial_write(COM1, "[XHCI] no XHCI controller found\n");
        return;
    }

    uint16_t vid = pci_read16(bus, dev, fn, PCI_VENDOR_ID);
    if (vid != 0x8086) {
        serial_write(COM1, "[XHCI] non-Intel XHCI (vid=0x");
        serial_write_hex(COM1, vid);
        serial_write(COM1, "), skipping USB2 port handoff\n");
        return;
    }

    uint32_t usb2prm = pci_read32(bus, dev, fn, 0xD4); /* USB2 Port Routing Mask */
    serial_write(COM1, "[XHCI] Intel XHCI found, USB2PRM=0x");
    serial_write_hex(COM1, usb2prm);

    if (usb2prm) {
        serial_write(COM1, " — releasing USB2 ports to EHCI\n");
        pci_write32(bus, dev, fn, 0xD4, 0);

        uint32_t usb3prm = pci_read32(bus, dev, fn, 0xD0); /* USB3 Port Routing Mask */
        serial_write(COM1, "[XHCI] USB3PRM=0x");
        serial_write_hex(COM1, usb3prm);
        serial_write(COM1, " (USB3 ports left with XHCI)\n");
    } else {
        serial_write(COM1, " — no USB2 ports owned by XHCI\n");
    }
}

/* ── Public: usb_init ── */

void usb_init(void)
{
    g_nctrl = 0;
    g_nhid  = 0;
    g_next_addr = 1;
    for (int i = 0; i < EHCI_MAX_HID; i++) g_hid[i].active = 0;

    /* On Intel PCH, XHCI owns all USB 2.0 ports at startup.
     * Release them to EHCI before scanning. */
    intel_xhci_handoff();

    /* Find up to MAX_EHCI_CTRL EHCI controllers */
    for (int n = 0; n < MAX_EHCI_CTRL; n++) {
        uint8_t bus, dev, fn;
        if (!pci_find_device_n(PCI_CLASS_SERIAL, PCI_SUB_USB, PCI_PROG_EHCI,
                               n, &bus, &dev, &fn))
            break;

        uint32_t bar = pci_bar_base(bus, dev, fn, 0);
        if (!bar) { serial_write(COM1, "[EHCI] BAR0=0, skip\n"); continue; }

        /* Mark the EHCI MMIO region uncacheable so register reads/writes
         * bypass the CPU cache (PSE PDE covering this address gets PCD+PWT). */
        paging_mark_uc(bar);

        serial_write(COM1, "[EHCI] Controller found BAR=");
        serial_write_hex(COM1, bar);
        serial_write(COM1, " bus=");
        serial_write_dec(COM1, bus);
        serial_write(COM1, " dev=");
        serial_write_dec(COM1, dev);
        serial_write(COM1, "\n");

        pci_enable_bus_mastering(bus, dev, fn);
        ehci_bios_handoff(bus, dev, fn, bar);

        uint8_t  caplength = (uint8_t)(cap_read(bar, EHCI_CAP_CAPLENGTH) & 0xFF);
        uint32_t op_base   = bar + caplength;
        uint32_t hcsparams = cap_read(bar, EHCI_CAP_HCSPARAMS);
        uint8_t  nports    = (uint8_t)(hcsparams & 0x0F);

        int ci = g_nctrl++;
        g_ctrl[ci].valid   = 1;
        g_ctrl[ci].pci_bus = bus;
        g_ctrl[ci].pci_dev = dev;
        g_ctrl[ci].pci_fn  = fn;
        g_ctrl[ci].bar     = bar;
        g_ctrl[ci].op_base = op_base;
        g_ctrl[ci].nports  = nports;
        for (int p = 0; p < MAX_ROOT_PORTS; p++) g_ctrl[ci].port_ccs[p] = 0;

        serial_write(COM1, "[EHCI] nports=");
        serial_write_dec(COM1, nports);
        serial_write(COM1, "\n");

        if (!ehci_reset_ctrl(ci)) {
            serial_write(COM1, "[EHCI] reset failed, skipping\n");
            g_nctrl--;
            continue;
        }

        scan_ports(ci);
    }

    /* Dump final controller register state before going live */
    for (int ci = 0; ci < g_nctrl; ci++) {
        uint32_t op = g_ctrl[ci].op_base;
        serial_write(COM1, "[EHCI] final ctrl=");
        serial_write_dec(COM1, (uint32_t)ci);
        serial_write(COM1, " USBCMD=0x");
        serial_write_hex(COM1, op_read(op, EHCI_OP_USBCMD));
        serial_write(COM1, " USBSTS=0x");
        serial_write_hex(COM1, op_read(op, EHCI_OP_USBSTS));
        serial_write(COM1, "\n");
        serial_write(COM1, "[EHCI] PERIODICBASE=0x");
        serial_write_hex(COM1, op_read(op, EHCI_OP_PERIODICBASE));
        serial_write(COM1, " ASYNCADDR=0x");
        serial_write_hex(COM1, op_read(op, EHCI_OP_ASYNCADDR));
        serial_write(COM1, "\n");
        serial_write(COM1, "[EHCI] frame_list@0x");
        serial_write_hex(COM1, (uint32_t)g_frame_list[ci]);
        serial_write(COM1, " [0]=0x");
        serial_write_hex(COM1, g_frame_list[ci][0]);
        serial_write(COM1, " [1]=0x");
        serial_write_hex(COM1, g_frame_list[ci][1]);
        serial_write(COM1, "\n");
    }

    g_ready = 1;
    serial_write(COM1, "[USB] init done, HID devices=");
    serial_write_dec(COM1, (uint32_t)g_nhid);
    serial_write(COM1, "\n");
}

/* ── Public: usb_poll (called from pit_timer_tick every tick) ── */

/* Hex-print one byte to COM1 */
static void poll_hex8(uint8_t v)
{
    static const char h[] = "0123456789ABCDEF";
    serial_putc(COM1, h[v >> 4]);
    serial_putc(COM1, h[v & 0xF]);
}

void usb_poll(void)
{
    if (!g_ready) return;

    /* Rate-limit counters */
    static uint32_t poll_tick   = 0;   /* counts every usb_poll() call    */
    static int      poll_hello  = 0;   /* 1 after first-call banner printed */

    /* Per-slot diagnostics */
    static uint8_t err_count[EHCI_MAX_HID];  /* consecutive errors per slot */
    static uint8_t first_rpt[EHCI_MAX_HID];  /* 1 after first good report   */

    poll_tick++;

    /* Print once to confirm the poll path is live */
    if (!poll_hello) {
        poll_hello = 1;
        serial_write(COM1, "[USB] poll active nhid=");
        serial_write_dec(COM1, (uint32_t)g_nhid);
        serial_write(COM1, "\n");
    }

    /* Every second: dump qTD/QH state + controller registers for each slot */
    if ((poll_tick % 100) == 0) {
        for (int slot = 0; slot < g_nhid; slot++) {
            ehci_hid_t *h = &g_hid[slot];
            if (!h->active) continue;
            ehci_qtd_t *qtd = &g_hid_qtd[slot];
            ehci_qh_t  *qh  = &g_hid_qh[slot];
            uint32_t    op  = g_ctrl[h->ctrl_idx].op_base;

            serial_write(COM1, "[USB] slot=");
            serial_write_dec(COM1, (uint32_t)slot);
            serial_write(COM1, " type=");
            serial_write(COM1, h->hid_type == HID_TYPE_KBD ? "KBD" : "MOUSE");
            serial_write(COM1, " qtd.tok=0x");
            serial_write_hex(COM1, qtd->token);
            serial_write(COM1, " qh.ov_tok=0x");
            serial_write_hex(COM1, qh->ov_token);
            serial_write(COM1, " qh.ov_next=0x");
            serial_write_hex(COM1, qh->ov_next);
            serial_write(COM1, "\n");

            serial_write(COM1, "[USB] USBCMD=0x");
            serial_write_hex(COM1, op_read(op, EHCI_OP_USBCMD));
            serial_write(COM1, " USBSTS=0x");
            serial_write_hex(COM1, op_read(op, EHCI_OP_USBSTS));
            serial_write(COM1, " PORT=0x");
            serial_write_hex(COM1, op_read(op, EHCI_OP_PORTSC(h->port)));
            serial_write(COM1, "\n");

            serial_write(COM1, "[USB] ep_char=0x");
            serial_write_hex(COM1, qh->ep_char);
            serial_write(COM1, " ep_cap=0x");
            serial_write_hex(COM1, qh->ep_cap);
            serial_write(COM1, "\n");
            serial_write(COM1, "[USB] framelist[0]=0x");
            serial_write_hex(COM1, g_frame_list[h->ctrl_idx][0]);
            serial_write(COM1, " QH@0x");
            serial_write_hex(COM1, (uint32_t)qh);
            serial_write(COM1, " qTD@0x");
            serial_write_hex(COM1, (uint32_t)qtd);
            serial_write(COM1, "\n");
        }
    }

    for (int slot = 0; slot < g_nhid; slot++) {
        ehci_hid_t *h = &g_hid[slot];
        if (!h->active) continue;

        ehci_qtd_t *qtd = &g_hid_qtd[slot];
        uint32_t tok = qtd->token;

        if (tok & QTD_ACTIVE) continue;  /* still in flight */

        /* Transfer complete — check for errors */
        if (tok & (QTD_ERR_MASK | QTD_HALTED)) {
            if (err_count[slot] < 5) {
                err_count[slot]++;
                serial_write(COM1, "[EHCI] qTD error slot=");
                serial_write_dec(COM1, (uint32_t)slot);
                serial_write(COM1, " tok=0x");
                serial_write_hex(COM1, tok);
                if (tok & QTD_HALTED)  serial_write(COM1, " HALTED");
                if (tok & QTD_BABBLE)  serial_write(COM1, " BABBLE");
                if (tok & QTD_DBERR)   serial_write(COM1, " BUFERR");
                if (tok & QTD_XACTERR) serial_write(COM1, " XACTERR");
                if (tok & QTD_MMF)     serial_write(COM1, " MMF");
                serial_write(COM1, "\n");
                uint32_t op = g_ctrl[h->ctrl_idx].op_base;
                serial_write(COM1, "[EHCI] USBSTS=0x");
                serial_write_hex(COM1, op_read(op, EHCI_OP_USBSTS));
                serial_write(COM1, " USBCMD=0x");
                serial_write_hex(COM1, op_read(op, EHCI_OP_USBCMD));
                serial_write(COM1, " PORTSC=0x");
                serial_write_hex(COM1, op_read(op, EHCI_OP_PORTSC(h->port)));
                serial_write(COM1, "\n");
            }
            hid_arm_qtd(slot);
            continue;
        }

        /* Successful report */
        err_count[slot] = 0;

        /* Hex-dump the first received report per slot */
        uint8_t *buf = g_hid_buf[slot];
        if (!first_rpt[slot]) {
            first_rpt[slot] = 1;
            if (h->hid_type == HID_TYPE_KBD)
                serial_write(COM1, "[USB KBD] first report: ");
            else
                serial_write(COM1, "[USB MOUSE] first report: ");
            for (int b = 0; b < 8; b++) {
                poll_hex8(buf[b]);
                serial_putc(COM1, ' ');
            }
            serial_write(COM1, "\n");
        }

        /* Route report to keyboard/mouse subsystem */
        if (h->hid_type == HID_TYPE_KBD) {
            usb_hid_kbd_report(buf, h->prev_report);
        } else if (h->hid_type == HID_TYPE_MOUSE) {
            usb_hid_mouse_report(buf);
        }

        /* Re-arm for next report */
        hid_arm_qtd(slot);
    }

    /* Hotplug: check for new connections on root hub ports */
    static uint32_t hotplug_tick = 0;
    hotplug_tick++;
    if (hotplug_tick < 50) return;  /* check every 500 ms */
    hotplug_tick = 0;

    for (int ci = 0; ci < g_nctrl; ci++) {
        uint32_t op = g_ctrl[ci].op_base;
        for (int p = 0; p < g_ctrl[ci].nports; p++) {
            uint32_t portsc = op_read(op, EHCI_OP_PORTSC(p));
            uint8_t ccs = (portsc & PORTSC_CCS) ? 1 : 0;
            uint8_t csc = (portsc & PORTSC_CSC) ? 1 : 0;

            if (csc) {
                /* Clear change bit */
                op_write(op, EHCI_OP_PORTSC(p),
                         (portsc & ~PORTSC_W1C_MASK) | PORTSC_CSC);

                if (ccs && !g_ctrl[ci].port_ccs[p]) {
                    /* New device connected */
                    serial_write(COM1, "[USB] hotplug: port ");
                    serial_write_dec(COM1, (uint32_t)p + 1);
                    serial_write(COM1, " connected\n");
                    g_ctrl[ci].port_ccs[p] = 1;
                    mdelay(100); /* debounce */
                    int speed = ehci_port_reset(ci, p);
                    if (speed >= 0) {
                        mdelay(10);
                        enumerate_device(ci, p, speed);
                    }
                } else if (!ccs && g_ctrl[ci].port_ccs[p]) {
                    /* Device removed */
                    serial_write(COM1, "[USB] hotplug: port ");
                    serial_write_dec(COM1, (uint32_t)p + 1);
                    serial_write(COM1, " disconnected\n");
                    g_ctrl[ci].port_ccs[p] = 0;
                    /* Invalidate any HID device on this port */
                    for (int s = 0; s < g_nhid; s++) {
                        if (g_hid[s].active &&
                            g_hid[s].ctrl_idx == (uint8_t)ci &&
                            g_hid[s].port == (uint8_t)p) {
                            g_hid[s].active = 0;
                            /* Remove from frame list (set frames to terminate) */
                            for (int f = 0; f < 1024; f++) {
                                if (g_frame_list[ci][f] ==
                                    (((uint32_t)&g_hid_qh[s] & ~0x1Fu) | HLP_TYPE_QH))
                                    g_frame_list[ci][f] = QTD_TERM;
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ── Public accessors for shell command ── */

int usb_get_ctrl_count(void) { return g_nctrl; }
int usb_get_hid_count(void)  { return g_nhid;  }

const ehci_hid_t *usb_get_hid(int idx) {
    if (idx < 0 || idx >= g_nhid) return (void *)0;
    return &g_hid[idx];
}

const char *usb_ctrl_info(int idx, char *buf, int bufsz)
{
    if (idx < 0 || idx >= g_nctrl || !g_ctrl[idx].valid) {
        if (bufsz > 0) buf[0] = '\0';
        return buf;
    }
    /* Build a simple info string */
    int pos = 0;
    const char *s = "EHCI bar=0x";
    while (*s && pos + 1 < bufsz) buf[pos++] = *s++;
    /* Print BAR hex manually (serial_write_hex writes to COM1, not buf) */
    uint32_t v = g_ctrl[idx].bar;
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (uint8_t)((v >> (i * 4)) & 0xF);
        char c = nibble < 10 ? (char)('0' + nibble) : (char)('A' + nibble - 10);
        if (pos + 1 < bufsz) buf[pos++] = c;
    }
    if (pos + 1 < bufsz) { buf[pos++] = ' '; }
    s = "ports=";
    while (*s && pos + 1 < bufsz) buf[pos++] = *s++;
    uint8_t np = g_ctrl[idx].nports;
    if (np >= 10 && pos + 1 < bufsz) buf[pos++] = (char)('0' + np / 10);
    if (pos + 1 < bufsz) buf[pos++] = (char)('0' + np % 10);
    buf[pos] = '\0';
    return buf;
}
