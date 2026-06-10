#ifndef SIMPLE_TTY_H
#define SIMPLE_TTY_H
#include "types.h"

#define NCCS 19

/* c_iflag bits */
#define ICRNL   0x0100
#define IXON    0x0400
/* c_oflag bits */
#define OPOST   0x0001
#define ONLCR   0x0004
/* c_lflag bits */
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080
#define TOSTOP  0x0100
#define IEXTEN  0x8000
/* c_cflag bits */
#define CS8     0x0030
#define CREAD   0x0080
#define CLOCAL  0x0800
#define HUPCL   0x0400
/* baud rates (dummy) */
#define B9600   0x000D
#define B38400  0x000F

/* c_cc indices */
#define VEOF    0
#define VEOL    1
#define VERASE  3
#define VINTR   8
#define VKILL   9
#define VMIN    16
#define VQUIT   17
#define VSUSP   10
#define VTIME   18
#define VSTART  12
#define VSTOP   13

/* struct termios */
typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[NCCS];
    uint32_t _ispeed;
    uint32_t _ospeed;
} __attribute__((packed)) termios_t;

/* ioctl request codes */
#define TCGETS    0x5401
#define TCSETS    0x5402
#define TCSETSW   0x5403
#define TCSETSF   0x5404
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCSCTTY  0x540E
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

/* Global TTY state */
typedef struct {
    termios_t termios;
    int       cols, rows;
} tty_state_t;

extern tty_state_t g_tty;

void tty_init(void);
void tty_default_termios(termios_t *t);
/* Returns 1 if char c is the interrupt character (Ctrl-C) */
int  tty_is_intr(char c);
/* Returns 1 if char c is the EOF character (Ctrl-D) */
int  tty_is_eof(char c);
/* Returns 1 if in canonical mode */
int  tty_is_canon(void);

/* ----------------------------------------------------------------
 * Raw-mode input queue.
 *
 * In non-canonical mode user programs read a terminal byte stream:
 * printable keys arrive as single bytes, special keys (arrows,
 * Delete, Home/End/PgUp/PgDn) arrive as VT100/xterm escape
 * sequences.  tty_pump() translates pending key events into that
 * byte stream without blocking; readers drain it with tty_getc().
 * ---------------------------------------------------------------- */

/* Translate all currently-pending keyboard events into queued bytes.
 * Non-blocking; safe to call from syscall context. */
void tty_pump(void);
/* 1 if at least one byte is queued (pumps first). */
int  tty_input_pending(void);
/* Pop one queued byte, or -1 if the queue is empty. Does NOT pump. */
int  tty_getc(void);
/* Discard all queued input (TCSETSF). */
void tty_flush_input(void);

#endif
