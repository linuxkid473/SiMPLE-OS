#include "console.h"
#include "keyboard.h"
#include "string.h"
#include "vga.h"

#define CONSOLE_HISTORY_SIZE 64
#define CONSOLE_HISTORY_LINE 256
#define CONSOLE_LINE_WIDTH   80

static char history[CONSOLE_HISTORY_SIZE][CONSOLE_HISTORY_LINE];
static uint32_t history_count = 0;
static uint32_t history_head = 0;

static void copy_limited(char* dst, const char* src, uint32_t cap) {
    if (cap == 0) {
        return;
    }

    uint32_t i = 0;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t line_capacity(uint16_t start_pos, uint32_t max_len) {
    uint32_t start_col = (uint32_t)(start_pos % CONSOLE_LINE_WIDTH);
    uint32_t line_cap = 0;
    if (start_col < (CONSOLE_LINE_WIDTH - 1)) {
        line_cap = (CONSOLE_LINE_WIDTH - 1) - start_col;
    }
    if (line_cap > (max_len - 1)) {
        line_cap = max_len - 1;
    }
    return line_cap;
}

static void history_add(const char* line) {
    if (!line || line[0] == '\0') {
        return;
    }

    if (history_count > 0) {
        uint32_t last = (history_head + CONSOLE_HISTORY_SIZE - 1) % CONSOLE_HISTORY_SIZE;
        if (strcmp(history[last], line) == 0) {
            return;
        }
    }

    copy_limited(history[history_head], line, CONSOLE_HISTORY_LINE);
    history_head = (history_head + 1) % CONSOLE_HISTORY_SIZE;
    if (history_count < CONSOLE_HISTORY_SIZE) {
        history_count++;
    }
}

static const char* history_get_from_newest(uint32_t offset) {
    if (offset >= history_count) {
        return "";
    }
    uint32_t idx = (history_head + CONSOLE_HISTORY_SIZE - 1 - offset) % CONSOLE_HISTORY_SIZE;
    return history[idx];
}

static void render_line(uint16_t start_pos, const char* buf, uint32_t len, uint32_t* prev_len, uint32_t cursor) {
    vga_set_cursor_pos(start_pos);

    for (uint32_t i = 0; i < len; i++) {
        vga_putc(buf[i]);
    }

    for (uint32_t i = len; i < *prev_len; i++) {
        vga_putc(' ');
    }

    *prev_len = len;
    vga_set_cursor_pos((uint16_t)(start_pos + cursor));
}

static int apply_single_completion(
    char* buffer,
    uint32_t* len,
    uint32_t* cursor,
    uint32_t line_cap,
    const console_completion_t* completion
) {
    uint32_t rs = completion->replace_start;
    uint32_t re = completion->replace_end;

    if (rs > *len || re > *len || rs > re) {
        return 0;
    }

    const char* replacement = completion->matches[0];
    uint32_t rep_len = (uint32_t)strlen(replacement);
    uint32_t removed = re - rs;
    uint32_t new_len = *len - removed + rep_len;

    if (new_len > line_cap || new_len >= CONSOLE_HISTORY_LINE) {
        return 0;
    }

    char tmp[CONSOLE_HISTORY_LINE];
    uint32_t out = 0;

    for (uint32_t i = 0; i < rs; i++) {
        tmp[out++] = buffer[i];
    }
    for (uint32_t i = 0; i < rep_len; i++) {
        tmp[out++] = replacement[i];
    }
    for (uint32_t i = re; i < *len; i++) {
        tmp[out++] = buffer[i];
    }
    tmp[out] = '\0';

    copy_limited(buffer, tmp, CONSOLE_HISTORY_LINE);
    *len = new_len;
    *cursor = rs + rep_len;
    return 1;
}

void console_read_line_opts(
    char* buffer,
    uint32_t max_len,
    int use_history,
    const char* initial_text,
    const char* prompt,
    console_completion_provider_t completion,
    void* completion_user
) {
    if (max_len == 0) {
        return;
    }

    if (prompt) {
        vga_write(prompt);
    }

    uint16_t start_pos = vga_get_cursor_pos();
    uint32_t line_cap = line_capacity(start_pos, max_len);

    if (initial_text) {
        copy_limited(buffer, initial_text, line_cap + 1);
    } else {
        buffer[0] = '\0';
    }

    uint32_t len = strlen(buffer);
    uint32_t cursor = len;
    uint32_t prev_len = 0;

    int history_nav = -1;
    char history_draft[CONSOLE_HISTORY_LINE];
    history_draft[0] = '\0';

    render_line(start_pos, buffer, len, &prev_len, cursor);

    while (1) {
        key_event_t event;
        keyboard_read_event(&event);

        if (event.type == KEY_EVENT_ENTER) {
            buffer[len] = '\0';
            vga_set_cursor_pos((uint16_t)(start_pos + len));
            vga_putc('\n');
            if (use_history) {
                history_add(buffer);
            }
            return;
        }

        if (event.type == KEY_EVENT_LEFT) {
            if (cursor > 0) {
                cursor--;
            }
            render_line(start_pos, buffer, len, &prev_len, cursor);
            continue;
        }

        if (event.type == KEY_EVENT_RIGHT) {
            if (cursor < len) {
                cursor++;
            }
            render_line(start_pos, buffer, len, &prev_len, cursor);
            continue;
        }

        if (event.type == KEY_EVENT_BACKSPACE) {
            if (cursor > 0) {
                for (uint32_t i = cursor - 1; i < len; i++) {
                    buffer[i] = buffer[i + 1];
                }
                cursor--;
                len--;
            }
            render_line(start_pos, buffer, len, &prev_len, cursor);
            continue;
        }

        if (event.type == KEY_EVENT_DELETE) {
            if (cursor < len) {
                for (uint32_t i = cursor; i < len; i++) {
                    buffer[i] = buffer[i + 1];
                }
                len--;
            }
            render_line(start_pos, buffer, len, &prev_len, cursor);
            continue;
        }

        if (use_history && event.type == KEY_EVENT_UP) {
            if (history_count == 0) {
                continue;
            }

            if (history_nav < 0) {
                copy_limited(history_draft, buffer, sizeof(history_draft));
            }

            if ((uint32_t)(history_nav + 1) < history_count) {
                history_nav++;
            }

            copy_limited(buffer, history_get_from_newest((uint32_t)history_nav), line_cap + 1);
            len = strlen(buffer);
            cursor = len;
            render_line(start_pos, buffer, len, &prev_len, cursor);
            continue;
        }

        if (use_history && event.type == KEY_EVENT_DOWN) {
            if (history_nav >= 0) {
                history_nav--;
                if (history_nav >= 0) {
                    copy_limited(buffer, history_get_from_newest((uint32_t)history_nav), line_cap + 1);
                } else {
                    copy_limited(buffer, history_draft, line_cap + 1);
                }
                len = strlen(buffer);
                cursor = len;
                render_line(start_pos, buffer, len, &prev_len, cursor);
            }
            continue;
        }

        if (completion && event.type == KEY_EVENT_CHAR && event.ch == '\t') {
            console_completion_t info;
            memset(&info, 0, sizeof(info));
            completion(buffer, cursor, &info, completion_user);

            if (info.match_count == 1) {
                if (apply_single_completion(buffer, &len, &cursor, line_cap, &info)) {
                    render_line(start_pos, buffer, len, &prev_len, cursor);
                }
            } else if (info.match_count > 1) {
                vga_set_cursor_pos((uint16_t)(start_pos + len));
                vga_putc('\n');
                for (uint32_t i = 0; i < info.match_count; i++) {
                    vga_write(info.matches[i]);
                    if (i + 1 < info.match_count) {
                        vga_write("  ");
                    }
                }
                vga_putc('\n');

                if (prompt) {
                    vga_write(prompt);
                }
                start_pos = vga_get_cursor_pos();
                line_cap = line_capacity(start_pos, max_len);
                if (len > line_cap) {
                    len = line_cap;
                    if (cursor > len) {
                        cursor = len;
                    }
                    buffer[len] = '\0';
                }
                prev_len = 0;
                render_line(start_pos, buffer, len, &prev_len, cursor);
            }
            continue;
        }

        if (event.type == KEY_EVENT_CHAR) {
            char c = event.ch;
            if (c < 32 || c > 126) {
                continue;
            }
            if (len >= line_cap) {
                continue;
            }

            for (uint32_t i = len; i > cursor; i--) {
                buffer[i] = buffer[i - 1];
            }
            buffer[cursor] = c;
            len++;
            cursor++;
            buffer[len] = '\0';

            render_line(start_pos, buffer, len, &prev_len, cursor);
            continue;
        }
    }
}

void console_read_line(char* buffer, uint32_t max_len) {
    console_read_line_opts(buffer, max_len, 0, NULL, NULL, NULL, NULL);
}
