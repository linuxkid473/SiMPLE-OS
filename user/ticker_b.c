/*
 * ticker_b.c — Phase 2 scheduler verification.
 * Continuously prints "B\n" with 1-second delays.
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    for (;;) {
        puts("B");
        sleep(1);
    }
    return 0;
}
