/*
 * tty.c — TTY/termios state management + raw-mode input byte queue.
 */
#include "tty.h"
#include "keyboard.h"
#include "vga.h"

tty_state_t g_tty;

/* ----------------------------------------------------------------
 * Raw-mode input queue
 * ---------------------------------------------------------------- */
#define TTY_INQ_SIZE 256
static uint8_t tty_inq[TTY_INQ_SIZE];
static uint16_t tty_inq_head = 0;   /* write index */
static uint16_t tty_inq_tail = 0;   /* read index  */

static void tty_inq_push(uint8_t c) {
    uint16_t next = (uint16_t)((tty_inq_head + 1u) % TTY_INQ_SIZE);
    if (next != tty_inq_tail) {      /* drop on overflow */
        tty_inq[tty_inq_head] = c;
        tty_inq_head = next;
    }
}

static void tty_inq_push_str(const char *s) {
    while (*s) tty_inq_push((uint8_t)*s++);
}

int tty_getc(void) {
    if (tty_inq_tail == tty_inq_head) return -1;
    int c = tty_inq[tty_inq_tail];
    tty_inq_tail = (uint16_t)((tty_inq_tail + 1u) % TTY_INQ_SIZE);
    return c;
}

void tty_flush_input(void) {
    tty_inq_tail = tty_inq_head;
}

/*
 * Translate every pending key event into terminal bytes.
 *
 * Mappings follow what an xterm-style terminal would send:
 *   Enter      → CR, then ICRNL turns it into NL (default termios has ICRNL)
 *   Backspace  → DEL (0x7F), matching the default VERASE
 *   Arrows     → ESC [ A/B/C/D
 *   Delete     → ESC [ 3 ~        Home/End  → ESC [ H / ESC [ F
 *   PgUp/PgDn  → ESC [ 5 ~ / ESC [ 6 ~
 *
 * Multi-byte sequences are queued atomically so a single read() can
 * return the whole sequence — programs rely on that to distinguish a
 * lone Esc keypress from an escape sequence.
 */
void tty_pump(void) {
    key_event_t ev;
    while (keyboard_poll_event(&ev)) {
        switch (ev.type) {
        case KEY_EVENT_CHAR: {
            uint8_t c = (uint8_t)ev.ch;
            if (c == '\r' && (g_tty.termios.c_iflag & ICRNL))
                c = '\n';
            tty_inq_push(c);
            break;
        }
        case KEY_EVENT_ENTER:
            tty_inq_push((g_tty.termios.c_iflag & ICRNL) ? (uint8_t)'\n'
                                                         : (uint8_t)'\r');
            break;
        case KEY_EVENT_BACKSPACE:
            tty_inq_push(0x7F);
            break;
        case KEY_EVENT_UP:     tty_inq_push_str("\033[A");  break;
        case KEY_EVENT_DOWN:   tty_inq_push_str("\033[B");  break;
        case KEY_EVENT_RIGHT:  tty_inq_push_str("\033[C");  break;
        case KEY_EVENT_LEFT:   tty_inq_push_str("\033[D");  break;
        case KEY_EVENT_DELETE: tty_inq_push_str("\033[3~"); break;
        case KEY_EVENT_HOME:   tty_inq_push_str("\033[H");  break;
        case KEY_EVENT_END:    tty_inq_push_str("\033[F");  break;
        case KEY_EVENT_PGUP:   tty_inq_push_str("\033[5~"); break;
        case KEY_EVENT_PGDN:   tty_inq_push_str("\033[6~"); break;
        default: break;
        }
    }
}

int tty_input_pending(void) {
    tty_pump();
    return tty_inq_tail != tty_inq_head;
}

void tty_default_termios(termios_t *t) {
    t->c_iflag = ICRNL | IXON;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = CS8 | CREAD | CLOCAL;
    t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    t->_ispeed = B38400;
    t->_ospeed = B38400;
    for (int i = 0; i < NCCS; i++) t->c_cc[i] = 0;
    t->c_cc[VEOF]   = 4;    /* Ctrl-D */
    t->c_cc[VERASE] = 127;  /* DEL / Backspace */
    t->c_cc[VINTR]  = 3;    /* Ctrl-C */
    t->c_cc[VQUIT]  = 28;   /* Ctrl-\ */
    t->c_cc[VSUSP]  = 26;   /* Ctrl-Z */
    t->c_cc[VMIN]   = 1;
    t->c_cc[VTIME]  = 0;
    t->c_cc[VSTART] = 17;   /* Ctrl-Q */
    t->c_cc[VSTOP]  = 19;   /* Ctrl-S */
}

void tty_init(void) {
    tty_default_termios(&g_tty.termios);
    /* Report the real character grid (framebuffer size / 8) so
     * full-screen programs use the whole display.  Falls back to
     * 80×25 when no framebuffer is active (VGA text mode). */
    uint32_t cols = 0, rows = 0;
    vga_text_dims(&cols, &rows);
    g_tty.cols = cols ? (int)cols : 80;
    g_tty.rows = rows ? (int)rows : 25;
}

int tty_is_intr(char c) {
    return (g_tty.termios.c_lflag & ISIG) &&
           (uint8_t)c == g_tty.termios.c_cc[VINTR];
}

int tty_is_eof(char c) {
    return (g_tty.termios.c_lflag & ICANON) &&
           (uint8_t)c == g_tty.termios.c_cc[VEOF];
}

int tty_is_canon(void) {
    return (g_tty.termios.c_lflag & ICANON) != 0;
}
