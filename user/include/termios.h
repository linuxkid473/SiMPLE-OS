#ifndef _TERMIOS_H
#define _TERMIOS_H

/*
 * Must match kernel/include/tty.h exactly (the kernel copies this struct
 * verbatim through the TCGETS/TCSETS ioctls).  The kernel struct is
 * packed: c_cc[19] is followed immediately by the speed fields.
 */

#define NCCS 19

typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
} __attribute__((packed));

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
/* baud (dummy) */
#define B9600   0x000D
#define B38400  0x000F

/* c_cc indices (match kernel/include/tty.h) */
#define VEOF    0
#define VEOL    1
#define VERASE  3
#define VINTR   8
#define VKILL   9
#define VSUSP   10
#define VSTART  12
#define VSTOP   13
#define VMIN    16
#define VQUIT   17
#define VTIME   18

/* tcsetattr actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int actions, const struct termios *t);
int tcgetpgrp(int fd);
int tcsetpgrp(int fd, int pgid);
int isatty(int fd);

#endif
