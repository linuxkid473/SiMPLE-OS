#ifndef SIMPLE_CONSOLE_H
#define SIMPLE_CONSOLE_H

#include "types.h"

#define CONSOLE_COMPLETION_MAX_MATCHES 16
#define CONSOLE_COMPLETION_TEXT_MAX    32

typedef struct {
    uint32_t replace_start;
    uint32_t replace_end;
    uint32_t match_count;
    char matches[CONSOLE_COMPLETION_MAX_MATCHES][CONSOLE_COMPLETION_TEXT_MAX];
} console_completion_t;

typedef void (*console_completion_provider_t)(
    const char* buffer,
    uint32_t cursor,
    console_completion_t* out,
    void* user
);

void console_read_line(char* buffer, uint32_t max_len);
void console_read_line_opts(
    char* buffer,
    uint32_t max_len,
    int use_history,
    const char* initial_text,
    const char* prompt,
    console_completion_provider_t completion,
    void* completion_user
);

#endif
