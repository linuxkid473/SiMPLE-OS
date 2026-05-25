#ifndef USER_WM_H
#define USER_WM_H

/* wm_event_t — matches the kernel-side definition in kernel/include/wm.h */
typedef struct {
    unsigned char  type;   /* 0=none 1=key_down 2=key_up 3=mouse_move 4=mouse_btn */
    unsigned short wid;    /* target window id */
    short          x, y;  /* mouse coords OR scancode (in x) for key events */
    unsigned char  btn;    /* mouse button mask: bit0=left bit1=right */
} __attribute__((packed)) wm_event_t;

#define WM_EV_NONE      0
#define WM_EV_KEY_DOWN  1
#define WM_EV_KEY_UP    2
#define WM_EV_MOUSE_MOV 3
#define WM_EV_MOUSE_BTN 4
#define WM_EV_CLOSE     5   /* user closed the window via the [X] button */

#define SC_ESC  0x01   /* PS/2 scancode for Escape */

#endif
