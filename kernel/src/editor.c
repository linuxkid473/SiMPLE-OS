#include "editor.h"
#include "console.h"
#include "keyboard.h"
#include "string.h"
#include "vga.h"

#define EDITOR_MAX_SIZE 16384
#define EDITOR_VIEW_ROWS 22
#define EDITOR_CMD_ROW 23
#define EDITOR_STATUS_ROW 24

static void index_to_line_col(const char* text, uint32_t len, uint32_t index, uint32_t* out_line, uint32_t* out_col) {
    uint32_t line = 0;
    uint32_t col = 0;

    if (index > len) {
        index = len;
    }

    for (uint32_t i = 0; i < index; i++) {
        if (text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }

    *out_line = line;
    *out_col = col;
}

static uint32_t line_col_to_index(const char* text, uint32_t len, uint32_t target_line, uint32_t target_col) {
    uint32_t line = 0;
    uint32_t i = 0;

    while (i < len && line < target_line) {
        if (text[i] == '\n') {
            line++;
        }
        i++;
    }

    uint32_t col = 0;
    while (i < len && text[i] != '\n' && col < target_col) {
        i++;
        col++;
    }

    return i;
}

static void set_status(char* status, const char* msg) {
    strncpy(status, msg, 79);
    status[79] = '\0';
}

static int save_file(fat16_fs_t* fs, uint16_t cwd_cluster, const char* filename, const char* text, uint32_t len, char* status) {
    int rc = fat16_write_file(fs, cwd_cluster, filename, text, len);
    if (rc == FAT16_OK) {
        set_status(status, "Saved.");
        return 1;
    }

    set_status(status, "Save failed.");
    return 0;
}

static void render_editor(
    const char* filename,
    const char* text,
    uint32_t len,
    uint32_t cursor,
    uint32_t top_line,
    const char* status
) {
    vga_clear();

    vga_write("Editing ");
    vga_write(filename);
    vga_write_line("  (:w save, :q quit, :wq save+quit)");

    uint32_t idx = line_col_to_index(text, len, top_line, 0);

    for (uint32_t row = 0; row < EDITOR_VIEW_ROWS; row++) {
        vga_set_cursor_pos((uint16_t)((row + 1) * 80));

        uint32_t col = 0;
        while (idx < len && text[idx] != '\n' && col < 79) {
            vga_putc(text[idx]);
            idx++;
            col++;
        }

        while (idx < len && text[idx] != '\n') {
            idx++;
        }
        if (idx < len && text[idx] == '\n') {
            idx++;
        }
    }

    vga_set_cursor_pos((uint16_t)(EDITOR_STATUS_ROW * 80));
    vga_write(status);

    uint32_t cursor_line = 0;
    uint32_t cursor_col = 0;
    index_to_line_col(text, len, cursor, &cursor_line, &cursor_col);

    if (cursor_line >= top_line && cursor_line < (top_line + EDITOR_VIEW_ROWS)) {
        uint32_t draw_row = 1 + (cursor_line - top_line);
        uint32_t draw_col = cursor_col;
        if (draw_col > 78) {
            draw_col = 78;
        }
        vga_set_cursor_pos((uint16_t)(draw_row * 80 + draw_col));
    }
}

void editor_open(fat16_fs_t* fs, uint16_t cwd_cluster, const char* filename) {
    static char text[EDITOR_MAX_SIZE];
    char status[80];

    uint32_t len = 0;
    int rc = fat16_read_file(fs, cwd_cluster, filename, text, sizeof(text), &len);
    if (rc == FAT16_ERR_NOT_FOUND) {
        text[0] = '\0';
        len = 0;
    } else if (rc != FAT16_OK) {
        vga_write_line("edit: failed to open file");
        return;
    }

    set_status(status, "Editing. Type :w, :q, or :wq");

    uint32_t cursor = len;
    uint32_t top_line = 0;
    uint32_t preferred_col = 0xFFFFFFFFU;

    while (1) {
        uint32_t cursor_line = 0;
        uint32_t cursor_col = 0;
        index_to_line_col(text, len, cursor, &cursor_line, &cursor_col);

        if (cursor_line < top_line) {
            top_line = cursor_line;
        }
        if (cursor_line >= top_line + EDITOR_VIEW_ROWS) {
            top_line = cursor_line - EDITOR_VIEW_ROWS + 1;
        }

        render_editor(filename, text, len, cursor, top_line, status);

        key_event_t event;
        keyboard_read_event(&event);

        if (event.type == KEY_EVENT_CHAR && event.ch == ':') {
            char cmd[16];
            vga_set_cursor_pos((uint16_t)(EDITOR_CMD_ROW * 80));
            vga_write(":");
            console_read_line_opts(cmd, sizeof(cmd), 0, NULL, NULL, NULL, NULL);

            char* c = cmd;
            while (*c == ' ') {
                c++;
            }
            if (*c == ':') {
                c++;
            }

            if (strcmp(c, "w") == 0) {
                save_file(fs, cwd_cluster, filename, text, len, status);
            } else if (strcmp(c, "q") == 0) {
                return;
            } else if (strcmp(c, "wq") == 0) {
                if (save_file(fs, cwd_cluster, filename, text, len, status)) {
                    return;
                }
            } else {
                set_status(status, "Unknown editor command");
            }
            preferred_col = 0xFFFFFFFFU;
            continue;
        }

        if (event.type == KEY_EVENT_LEFT) {
            if (cursor > 0) {
                cursor--;
            }
            preferred_col = 0xFFFFFFFFU;
            continue;
        }

        if (event.type == KEY_EVENT_RIGHT) {
            if (cursor < len) {
                cursor++;
            }
            preferred_col = 0xFFFFFFFFU;
            continue;
        }

        if (event.type == KEY_EVENT_UP || event.type == KEY_EVENT_DOWN) {
            index_to_line_col(text, len, cursor, &cursor_line, &cursor_col);
            if (preferred_col == 0xFFFFFFFFU) {
                preferred_col = cursor_col;
            }

            if (event.type == KEY_EVENT_UP) {
                if (cursor_line > 0) {
                    cursor = line_col_to_index(text, len, cursor_line - 1, preferred_col);
                }
            } else {
                uint32_t next_line = cursor_line + 1;
                uint32_t probe = line_col_to_index(text, len, next_line, 0);
                if (probe <= len && (probe < len || cursor_line == 0 || text[len - 1] == '\n')) {
                    cursor = line_col_to_index(text, len, next_line, preferred_col);
                }
            }
            continue;
        }

        if (event.type == KEY_EVENT_BACKSPACE) {
            if (cursor > 0) {
                for (uint32_t i = cursor - 1; i < len; i++) {
                    text[i] = text[i + 1];
                }
                cursor--;
                len--;
                text[len] = '\0';
            }
            preferred_col = 0xFFFFFFFFU;
            continue;
        }

        if (event.type == KEY_EVENT_DELETE) {
            if (cursor < len) {
                for (uint32_t i = cursor; i < len; i++) {
                    text[i] = text[i + 1];
                }
                len--;
                text[len] = '\0';
            }
            preferred_col = 0xFFFFFFFFU;
            continue;
        }

        if (event.type == KEY_EVENT_ENTER) {
            if (len + 1 >= EDITOR_MAX_SIZE) {
                set_status(status, "Buffer full");
                continue;
            }
            for (uint32_t i = len; i > cursor; i--) {
                text[i] = text[i - 1];
            }
            text[cursor] = '\n';
            cursor++;
            len++;
            text[len] = '\0';
            preferred_col = 0xFFFFFFFFU;
            continue;
        }

        if (event.type == KEY_EVENT_CHAR && event.ch >= 32 && event.ch <= 126) {
            index_to_line_col(text, len, cursor, &cursor_line, &cursor_col);
            if (cursor_col >= 78) {
                set_status(status, "Line width limit reached");
                continue;
            }
            if (len + 1 >= EDITOR_MAX_SIZE) {
                set_status(status, "Buffer full");
                continue;
            }

            for (uint32_t i = len; i > cursor; i--) {
                text[i] = text[i - 1];
            }
            text[cursor] = event.ch;
            cursor++;
            len++;
            text[len] = '\0';
            preferred_col = 0xFFFFFFFFU;
            continue;
        }
    }
}
