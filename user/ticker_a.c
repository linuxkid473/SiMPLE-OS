/*
 * ticker_a.c — Phase 2 scheduler verification.
 * Continuously prints "A\n" with 1-second delays.
 * Run alongside ticker_b and ticker_c to verify the scheduler
 * rotates correctly and no process starves.
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    for (;;) {
        puts("A");
        sleep(1);
    }
    return 0;
}
