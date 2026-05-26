/*
 * tty.c — TTY/termios state management.
 */
#include "tty.h"

tty_state_t g_tty;

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
    g_tty.cols = 80;
    g_tty.rows = 25;
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
