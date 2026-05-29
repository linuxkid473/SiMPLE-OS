/*
 * ticker_c.c — Phase 2 scheduler verification.
 * Continuously prints "C\n" with 1-second delays.
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    for (;;) {
        puts("C");
        sleep(1);
    }
    return 0;
}
