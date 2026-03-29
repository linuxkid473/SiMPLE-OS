#include "shell.h"
#include "console.h"
#include "editor.h"
#include "fat16.h"
#include "keyboard.h"
#include "power.h"
#include "string.h"
#include "vga.h"

#define SHELL_LINE_MAX 256
#define CAT_BUFFER_MAX 8192
#define SHELL_MAX_DEPTH 16
#define SHELL_NAME_MAX 13
#define SHELL_PROMPT_MAX 128
#define SHELL_COMPLETION_ENTRIES 256
#define ABOUT_LABEL_WIDTH 16

#define COLOR_BLACK      0x0
#define COLOR_LIGHT_GREY 0x7
#define COLOR_LIGHT_CYAN 0xB
#define COLOR_YELLOW     0xE
#define COLOR_WHITE      0xF

static uint16_t cwd_cluster = 0;
static char cwd_parts[SHELL_MAX_DEPTH][SHELL_NAME_MAX];
static uint32_t cwd_depth = 0;
static char prompt_buf[SHELL_PROMPT_MAX];
static char file_buf[CAT_BUFFER_MAX];
static uint32_t boot_memory_kb = 0;
static int boot_memory_known = 0;
static int boot_multiboot_ok = 0;
static uint32_t creepy_rng_state = 0xC0FFEE12U;

static const char* command_names[] = {
    "help", "about", "ilovelinux", "clear", "echo", "ls", "cd", "open", "edit", "touch", "mkdir", "rm", "cp", "mv", "poweroff", "reboot"
};
static const uint32_t command_count = sizeof(command_names) / sizeof(command_names[0]);

static void append_char(char* dst, uint32_t cap, uint32_t* io_len, char c);
static void append_str(char* dst, uint32_t cap, uint32_t* io_len, const char* src);
static int resolve_parent_and_name(
    fat16_fs_t* fs,
    const char* path,
    uint16_t* out_parent_cluster,
    char out_name[SHELL_NAME_MAX]
);

typedef struct {
    fat16_fs_t* fs;
    int fs_ready;
    uint16_t cwd_cluster;
    char cwd_parts[SHELL_MAX_DEPTH][SHELL_NAME_MAX];
    uint32_t cwd_depth;
} completion_ctx_t;

static int is_space_char(char c) {
    return c == ' ' || c == '\t';
}

static char* skip_spaces(char* s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

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

void shell_set_boot_info(uint32_t memory_kb, int memory_known, int multiboot_ok) {
    boot_memory_kb = memory_kb;
    boot_memory_known = memory_known;
    boot_multiboot_ok = multiboot_ok;
}

static void u32_to_dec(uint32_t value, char* out, uint32_t cap) {
    if (cap == 0) {
        return;
    }

    if (value == 0) {
        if (cap > 1) {
            out[0] = '0';
            out[1] = '\0';
        } else {
            out[0] = '\0';
        }
        return;
    }

    char tmp[11];
    uint32_t pos = 0;
    while (value > 0 && pos < sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    uint32_t out_pos = 0;
    while (pos > 0 && out_pos + 1 < cap) {
        out[out_pos++] = tmp[--pos];
    }
    out[out_pos] = '\0';
}

static void about_separator(char ch) {
    for (uint32_t i = 0; i < 30; i++) {
        vga_putc(ch);
    }
    vga_putc('\n');
}

static void about_print_kv(const char* label, const char* value) {
    const char* shown = (value && *value) ? value : "Unknown";

    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    vga_write(label);

    size_t len = strlen(label);
    while (len < ABOUT_LABEL_WIDTH) {
        vga_putc(' ');
        len++;
    }

    vga_write(": ");
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_write_line(shown);
}

static void about_print_commands(void) {
    const uint32_t col_width = 12;
    const uint32_t cols = 5;

    for (uint32_t i = 0; i < command_count; i++) {
        vga_set_color(COLOR_WHITE, COLOR_BLACK);
        vga_write(command_names[i]);

        size_t len = strlen(command_names[i]);
        uint32_t pad = (len < col_width) ? (col_width - (uint32_t)len) : 1;
        if ((i + 1) % cols == 0 || i + 1 == command_count) {
            vga_putc('\n');
        } else {
            for (uint32_t p = 0; p < pad; p++) {
                vga_putc(' ');
            }
        }
    }
}

static void shell_about(void) {
    char memory_status[32];
    char number[16];
    const char* boot_method = boot_multiboot_ok ? "BIOS (GRUB Multiboot)" : "Unknown";

    if (boot_memory_known) {
        uint32_t mem_mb = boot_memory_kb / 1024U;
        u32_to_dec(mem_mb > 0 ? mem_mb : boot_memory_kb, number, sizeof(number));

        uint32_t pos = 0;
        memory_status[0] = '\0';
        append_str(memory_status, sizeof(memory_status), &pos, number);
        if (mem_mb > 0) {
            append_str(memory_status, sizeof(memory_status), &pos, " MB");
        } else {
            append_str(memory_status, sizeof(memory_status), &pos, " KB");
        }
    } else {
        copy_limited(memory_status, "Unknown", sizeof(memory_status));
    }

    vga_clear();

    vga_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    about_separator('=');
    vga_write_line("SYSTEM INFO");
    about_separator('=');
    vga_putc('\n');

    vga_set_color(COLOR_YELLOW, COLOR_BLACK);
    vga_write_line("Core");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    about_separator('-');

    about_print_kv("OS Name", "SiMPLE OS v0.1");
    about_print_kv("Kernel Mode", "32-bit Protected Mode");
    about_print_kv("Architecture", "x86");
    about_print_kv("Memory", memory_status);
    about_print_kv("Boot Method", boot_method);
    about_print_kv("System ID", "SiMPLE-x86");
    vga_putc('\n');

    vga_set_color(COLOR_YELLOW, COLOR_BLACK);
    vga_write_line("Available Commands");
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    about_separator('-');
    about_print_commands();

    vga_set_color(COLOR_WHITE, COLOR_BLACK);
}

static void delay_ticks(uint32_t ticks) {
    volatile uint32_t i;
    for (i = 0; i < ticks; i++) {
        __asm__ volatile("nop");
    }
}

static uint32_t creepy_rand(void) {
    creepy_rng_state = creepy_rng_state * 1664525U + 1013904223U;
    return creepy_rng_state;
}

static void creepy_glitch_chars(uint32_t count) {
    uint16_t saved = vga_get_cursor_pos();
    for (uint32_t i = 0; i < count; i++) {
        uint16_t pos = (uint16_t)(creepy_rand() % (80U * 25U));
        char ch = (char)('!' + (creepy_rand() % 94U));

        vga_set_color(COLOR_WHITE, COLOR_BLACK);
        vga_set_cursor_pos(pos);
        vga_putc(ch);
        delay_ticks(250000U);

        vga_set_cursor_pos(pos);
        vga_putc(' ');
    }
    vga_set_cursor_pos(saved);
}

static void creepy_type_line(const char* text) {
    while (*text) {
        vga_putc(*text++);
        delay_ticks(1200000U);
        if ((creepy_rand() & 0x07U) == 0) {
            creepy_glitch_chars(2);
            vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        }
    }
    vga_putc('\n');
}

static void creepy_flicker(void) {
    for (uint32_t i = 0; i < 4; i++) {
        vga_clear();
        vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        vga_write_line("You chose the wrong OS...");
        creepy_glitch_chars(6);
        delay_ticks(3000000U);
        vga_clear();
        delay_ticks(1200000U);
    }
}

static int is_protected_path(const char* path) {
    return strcmp(path, "/grub/grub.cfg") == 0 ||
           strcmp(path, "/boot/kernel.bin") == 0 ||
           strcmp(path, "kernel.bin") == 0;
}

static int make_backup_name(const char* file_name, char* out, uint32_t cap) {
    uint32_t i = 0;
    while (file_name[i] && file_name[i] != '.' && i < 8 && i + 5 < cap) {
        out[i] = file_name[i];
        i++;
    }

    if (i == 0 || i + 4 >= cap) {
        return FAT16_ERR_INVALID;
    }

    out[i++] = '.';
    out[i++] = 'b';
    out[i++] = 'a';
    out[i++] = 'k';
    out[i] = '\0';
    return FAT16_OK;
}

static void backup_protected_file_if_needed(fat16_fs_t* fs, const char* path) {
    if (!is_protected_path(path)) {
        return;
    }

    uint16_t dir_cluster = cwd_cluster;
    char file_name[SHELL_NAME_MAX];
    if (resolve_parent_and_name(fs, path, &dir_cluster, file_name) != FAT16_OK) {
        return;
    }

    fat16_dirent_t entry;
    if (fat16_stat(fs, dir_cluster, file_name, &entry) != FAT16_OK) {
        return;
    }
    if (entry.attr & FAT16_ATTR_DIRECTORY) {
        return;
    }

    char backup_name[SHELL_NAME_MAX];
    if (make_backup_name(file_name, backup_name, sizeof(backup_name)) != FAT16_OK) {
        return;
    }

    (void)fat16_copy_file(fs, dir_cluster, file_name, dir_cluster, backup_name);
}

static int confirm_delete(void) {
    vga_write("Are you sure? (y/n) ");
    char answer = 0;
    while (1) {
        answer = keyboard_getchar();
        if (answer == 'y' || answer == 'Y' || answer == 'n' || answer == 'N') {
            break;
        }
    }
    vga_putc(answer);
    vga_putc('\n');
    return answer == 'y' || answer == 'Y';
}

static void creepy_mode(void) {
    key_event_t event;
    vga_write_line("Do you really?");
    keyboard_read_event(&event);

    vga_clear();
    vga_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
    creepy_type_line("You chose the wrong OS...");
    creepy_type_line("This system is watching...");
    creepy_type_line("There is no escape...");

    creepy_flicker();

    vga_clear();
    vga_set_color(COLOR_WHITE, COLOR_BLACK);
    vga_write_line("   (ಠ_ಠ)");
    vga_write_line("  /|   |");
    vga_write_line(" /||");
    vga_write_line("   /   ");
    vga_write_line("  /____");
    vga_putc('\n');
    vga_write_line("Do you still love Linux?");
    vga_putc('\n');

    for (int i = 5; i >= 0; i--) {
        char number[12];
        u32_to_dec((uint32_t)i, number, sizeof(number));
        vga_write("Shutting down in ");
        vga_write(number);
        vga_write_line("...");
        delay_ticks(90000000U);
    }

    poweroff();
}

static void trim_end(char* s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
}

static void append_char(char* dst, uint32_t cap, uint32_t* io_len, char c) {
    if (*io_len + 1 >= cap) {
        return;
    }
    dst[*io_len] = c;
    *io_len = *io_len + 1;
    dst[*io_len] = '\0';
}

static void append_str(char* dst, uint32_t cap, uint32_t* io_len, const char* src) {
    while (src && *src) {
        if (*io_len + 1 >= cap) {
            return;
        }
        dst[*io_len] = *src;
        *io_len = *io_len + 1;
        src++;
    }
    dst[*io_len] = '\0';
}

static void build_prompt(void) {
    uint32_t len = 0;
    prompt_buf[0] = '\0';

    append_str(prompt_buf, sizeof(prompt_buf), &len, "SiMPLE ");
    append_str(prompt_buf, sizeof(prompt_buf), &len, "~");

    for (uint32_t i = 0; i < cwd_depth; i++) {
        append_char(prompt_buf, sizeof(prompt_buf), &len, '/');
        append_str(prompt_buf, sizeof(prompt_buf), &len, cwd_parts[i]);
    }

    append_str(prompt_buf, sizeof(prompt_buf), &len, " > ");
}

static void copy_parts(
    char (*dst)[SHELL_NAME_MAX],
    const char (*src)[SHELL_NAME_MAX],
    uint32_t depth
) {
    for (uint32_t i = 0; i < depth && i < SHELL_MAX_DEPTH; i++) {
        copy_limited(dst[i], src[i], SHELL_NAME_MAX);
    }
}

static char* next_token(char** cursor) {
    if (!cursor || !*cursor) {
        return NULL;
    }

    char* s = skip_spaces(*cursor);
    if (*s == '\0') {
        *cursor = s;
        return NULL;
    }

    char* start = s;
    while (*s && !is_space_char(*s)) {
        s++;
    }

    if (*s) {
        *s = '\0';
        s++;
    }

    *cursor = s;
    return start;
}

static uint32_t min3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t m = a;
    if (b < m) {
        m = b;
    }
    if (c < m) {
        m = c;
    }
    return m;
}

static uint32_t edit_distance(const char* a, const char* b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);

    if (la > 15 || lb > 15) {
        return 99;
    }

    uint32_t prev[17];
    uint32_t curr[17];

    for (size_t j = 0; j <= lb; j++) {
        prev[j] = (uint32_t)j;
    }

    for (size_t i = 1; i <= la; i++) {
        curr[0] = (uint32_t)i;
        for (size_t j = 1; j <= lb; j++) {
            uint32_t cost = (a[i - 1] == b[j - 1]) ? 0U : 1U;
            curr[j] = min3(
                prev[j] + 1U,
                curr[j - 1] + 1U,
                prev[j - 1] + cost
            );
        }
        for (size_t j = 0; j <= lb; j++) {
            prev[j] = curr[j];
        }
    }

    return prev[lb];
}

static const char* suggest_command(const char* input) {
    const char* best = NULL;
    uint32_t best_dist = 99;

    for (uint32_t i = 0; i < command_count; i++) {
        uint32_t dist = edit_distance(input, command_names[i]);
        if (dist < best_dist) {
            best_dist = dist;
            best = command_names[i];
        }
    }

    return (best_dist <= 2) ? best : NULL;
}

static void print_label_target(const char* label, const char* target) {
    vga_write(label);
    vga_write(target);
    vga_putc('\n');
}

static void print_generic_fs_error(int rc) {
    if (rc == FAT16_ERR_INVALID) {
        vga_write_line("Invalid name (8.3 format required)");
    } else if (rc == FAT16_ERR_NOSPACE) {
        vga_write_line("No space left on device");
    } else if (rc == FAT16_ERR_EXISTS) {
        vga_write_line("Already exists");
    } else if (rc == FAT16_ERR_NOTDIR) {
        vga_write_line("Not a directory");
    } else if (rc == FAT16_ERR_ISDIR) {
        vga_write_line("Is a directory");
    } else if (rc == FAT16_ERR_IO) {
        vga_write_line("Filesystem I/O error");
    } else if (rc == FAT16_ERR_NOTEMPTY) {
        vga_write_line("Directory not empty");
    } else if (rc == FAT16_ERR_NOT_FOUND) {
        vga_write_line("Not found");
    } else {
        vga_write_line("Filesystem error");
    }
}

static int starts_with_nocase(const char* value, const char* prefix) {
    while (*prefix) {
        char a = *value;
        char b = *prefix;

        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }

        if (a != b) {
            return 0;
        }
        value++;
        prefix++;
    }

    return 1;
}

static int resolve_dir_path_with_base(
    fat16_fs_t* fs,
    uint16_t base_cluster,
    const char (*base_parts)[SHELL_NAME_MAX],
    uint32_t base_depth,
    const char* path,
    uint16_t* out_cluster,
    char (*out_parts)[SHELL_NAME_MAX],
    uint32_t* out_depth
) {
    if (!path || !out_cluster) {
        return FAT16_ERR_INVALID;
    }

    char temp_parts[SHELL_MAX_DEPTH][SHELL_NAME_MAX];
    uint32_t depth = 0;
    uint16_t cluster = 0;
    const char* p = path;

    if (*p == '\0') {
        cluster = base_cluster;
        depth = base_depth;
        copy_parts(temp_parts, base_parts, base_depth);
    } else if (*p == '~') {
        if (p[1] != '\0' && p[1] != '/') {
            return FAT16_ERR_INVALID;
        }
        cluster = 0;
        depth = 0;
        p++;
    } else if (*p == '/') {
        cluster = 0;
        depth = 0;
    } else {
        cluster = base_cluster;
        depth = base_depth;
        copy_parts(temp_parts, base_parts, base_depth);
    }

    while (*p == '/') {
        p++;
    }

    while (*p) {
        char segment[SHELL_NAME_MAX];
        uint32_t seg_len = 0;

        while (*p && *p != '/') {
            if (seg_len + 1 >= SHELL_NAME_MAX) {
                return FAT16_ERR_INVALID;
            }
            segment[seg_len++] = *p;
            p++;
        }
        segment[seg_len] = '\0';

        while (*p == '/') {
            p++;
        }

        if (segment[0] == '\0' || strcmp(segment, ".") == 0) {
            continue;
        }

        if (strcmp(segment, "..") == 0) {
            uint16_t next = cluster;
            int rc = fat16_change_dir(fs, cluster, "..", &next);
            if (rc != FAT16_OK) {
                return rc;
            }

            cluster = next;
            if (depth > 0) {
                depth--;
            }
            continue;
        }

        uint16_t next = cluster;
        int rc = fat16_change_dir(fs, cluster, segment, &next);
        if (rc != FAT16_OK) {
            return rc;
        }

        cluster = next;
        if (depth >= SHELL_MAX_DEPTH) {
            return FAT16_ERR_INVALID;
        }
        copy_limited(temp_parts[depth], segment, SHELL_NAME_MAX);
        depth++;
    }

    *out_cluster = cluster;
    if (out_parts) {
        copy_parts(out_parts, (const char (*)[SHELL_NAME_MAX])temp_parts, depth);
    }
    if (out_depth) {
        *out_depth = depth;
    }

    return FAT16_OK;
}

static int resolve_parent_and_name(
    fat16_fs_t* fs,
    const char* path,
    uint16_t* out_parent_cluster,
    char out_name[SHELL_NAME_MAX]
) {
    if (!path || !out_parent_cluster || !out_name) {
        return FAT16_ERR_INVALID;
    }

    size_t len = strlen(path);
    if (len == 0) {
        return FAT16_ERR_INVALID;
    }
    if (path[len - 1] == '/') {
        return FAT16_ERR_INVALID;
    }
    if (path[0] == '~' && path[1] != '\0' && path[1] != '/') {
        return FAT16_ERR_INVALID;
    }

    int last_slash = -1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') {
            last_slash = (int)i;
        }
    }

    if (last_slash < 0) {
        if (strcmp(path, "~") == 0 || strcmp(path, "/") == 0) {
            return FAT16_ERR_INVALID;
        }
        copy_limited(out_name, path, SHELL_NAME_MAX);
        *out_parent_cluster = cwd_cluster;
        return FAT16_OK;
    }

    const char* leaf = path + last_slash + 1;
    if (*leaf == '\0') {
        return FAT16_ERR_INVALID;
    }
    copy_limited(out_name, leaf, SHELL_NAME_MAX);

    char parent_path[SHELL_LINE_MAX];
    if (last_slash == 0) {
        copy_limited(parent_path, "/", sizeof(parent_path));
    } else {
        if ((uint32_t)last_slash >= sizeof(parent_path)) {
            return FAT16_ERR_INVALID;
        }
        for (int i = 0; i < last_slash; i++) {
            parent_path[i] = path[i];
        }
        parent_path[last_slash] = '\0';
    }

    return resolve_dir_path_with_base(
        fs,
        cwd_cluster,
        (const char (*)[SHELL_NAME_MAX])cwd_parts,
        cwd_depth,
        parent_path,
        out_parent_cluster,
        NULL,
        NULL
    );
}

static void shell_open_file(
    fat16_fs_t* fs,
    uint16_t dir_cluster,
    const char* name,
    const char* shown_name
) {
    uint32_t out_len = 0;
    int rc = fat16_read_file(fs, dir_cluster, name, file_buf, sizeof(file_buf), &out_len);
    if (rc == FAT16_ERR_NOT_FOUND) {
        print_label_target("File not found: ", shown_name);
        return;
    }
    if (rc == FAT16_ERR_ISDIR) {
        print_label_target("Is a directory: ", shown_name);
        return;
    }
    if (rc != FAT16_OK) {
        print_generic_fs_error(rc);
        return;
    }

    vga_write(file_buf);
    if (out_len == 0 || file_buf[out_len - 1] != '\n') {
        vga_putc('\n');
    }
}

static void shell_help(void) {
    vga_write_line("Commands:");
    vga_write_line("  help           - list commands");
    vga_write_line("  about          - show system information");
    vga_write_line("  ilovelinux     - enter creepy mode");
    vga_write_line("  clear          - clear screen");
    vga_write_line("  echo <text>    - print text");
    vga_write_line("  ls [dir]       - list directory contents");
    vga_write_line("  cd <dir>       - change directory");
    vga_write_line("  open <file>    - display file contents");
    vga_write_line("  edit <file>    - open text editor");
    vga_write_line("  touch <file>   - create file");
    vga_write_line("  mkdir <dir>    - create directory");
    vga_write_line("  rm <name>      - remove file or empty directory");
    vga_write_line("  cp <src> <dst> - copy file");
    vga_write_line("  mv <src> <dst> - move/rename file");
    vga_write_line("  poweroff       - power off machine");
    vga_write_line("  reboot         - reboot machine");
}

static void completion_sort(console_completion_t* out) {
    for (uint32_t i = 0; i + 1 < out->match_count; i++) {
        for (uint32_t j = i + 1; j < out->match_count; j++) {
            if (strcmp(out->matches[i], out->matches[j]) > 0) {
                char tmp[CONSOLE_COMPLETION_TEXT_MAX];
                copy_limited(tmp, out->matches[i], sizeof(tmp));
                copy_limited(out->matches[i], out->matches[j], CONSOLE_COMPLETION_TEXT_MAX);
                copy_limited(out->matches[j], tmp, CONSOLE_COMPLETION_TEXT_MAX);
            }
        }
    }
}

static void completion_add(console_completion_t* out, const char* text) {
    if (out->match_count >= CONSOLE_COMPLETION_MAX_MATCHES) {
        return;
    }
    copy_limited(out->matches[out->match_count], text, CONSOLE_COMPLETION_TEXT_MAX);
    out->match_count++;
}

static uint32_t count_tokens_before(const char* buffer, uint32_t limit) {
    uint32_t i = 0;
    uint32_t count = 0;
    while (i < limit) {
        while (i < limit && is_space_char(buffer[i])) {
            i++;
        }
        if (i >= limit) {
            break;
        }
        count++;
        while (i < limit && !is_space_char(buffer[i])) {
            i++;
        }
    }
    return count;
}

static void extract_command_token(const char* buffer, char* out, uint32_t out_cap) {
    uint32_t i = 0;
    uint32_t j = 0;

    while (buffer[i] && is_space_char(buffer[i])) {
        i++;
    }
    while (buffer[i] && !is_space_char(buffer[i]) && j + 1 < out_cap) {
        out[j++] = buffer[i++];
    }
    out[j] = '\0';
}

static void shell_completion(
    const char* buffer,
    uint32_t cursor,
    console_completion_t* out,
    void* user
) {
    completion_ctx_t* ctx = (completion_ctx_t*)user;
    uint32_t len = (uint32_t)strlen(buffer);
    if (cursor > len) {
        cursor = len;
    }

    out->replace_start = cursor;
    out->replace_end = cursor;
    out->match_count = 0;

    uint32_t token_start = cursor;
    while (token_start > 0 && !is_space_char(buffer[token_start - 1])) {
        token_start--;
    }

    uint32_t token_count = count_tokens_before(buffer, token_start);
    out->replace_start = token_start;
    out->replace_end = cursor;

    char prefix[SHELL_LINE_MAX];
    uint32_t prefix_len = cursor - token_start;
    if (prefix_len >= sizeof(prefix)) {
        return;
    }
    for (uint32_t i = 0; i < prefix_len; i++) {
        prefix[i] = buffer[token_start + i];
    }
    prefix[prefix_len] = '\0';

    if (token_count == 0) {
        for (uint32_t i = 0; i < command_count; i++) {
            if (starts_with_nocase(command_names[i], prefix)) {
                completion_add(out, command_names[i]);
            }
        }
        completion_sort(out);
        return;
    }

    if (!ctx || !ctx->fs_ready || !ctx->fs) {
        return;
    }

    char cmd[16];
    extract_command_token(buffer, cmd, sizeof(cmd));

    int dirs_only = 0;
    if (strcmp(cmd, "cd") == 0) {
        dirs_only = 1;
    }

    char path_prefix[SHELL_LINE_MAX];
    char name_prefix[SHELL_LINE_MAX];
    name_prefix[0] = '\0';
    uint16_t target_dir = ctx->cwd_cluster;

    int last_slash = -1;
    for (uint32_t i = 0; i < prefix_len; i++) {
        if (prefix[i] == '/') {
            last_slash = (int)i;
        }
    }

    if (last_slash >= 0) {
        if ((uint32_t)last_slash + 1 >= sizeof(path_prefix) || prefix_len - (uint32_t)last_slash >= sizeof(name_prefix)) {
            return;
        }

        for (int i = 0; i <= last_slash; i++) {
            path_prefix[i] = prefix[i];
        }
        path_prefix[last_slash + 1] = '\0';

        for (uint32_t i = 0; i + (uint32_t)last_slash + 1 < prefix_len; i++) {
            name_prefix[i] = prefix[(uint32_t)last_slash + 1 + i];
            name_prefix[i + 1] = '\0';
        }

        char dir_expr[SHELL_LINE_MAX];
        if (last_slash == 0) {
            copy_limited(dir_expr, "/", sizeof(dir_expr));
        } else {
            for (int i = 0; i < last_slash; i++) {
                dir_expr[i] = prefix[i];
            }
            dir_expr[last_slash] = '\0';
        }

        if (resolve_dir_path_with_base(
                ctx->fs,
                ctx->cwd_cluster,
                (const char (*)[SHELL_NAME_MAX])ctx->cwd_parts,
                ctx->cwd_depth,
                dir_expr,
                &target_dir,
                NULL,
                NULL
            ) != FAT16_OK) {
            return;
        }
    } else {
        path_prefix[0] = '\0';
        copy_limited(name_prefix, prefix, sizeof(name_prefix));
    }

    fat16_dirent_t entries[SHELL_COMPLETION_ENTRIES];
    int count = 0;
    if (fat16_list_entries(ctx->fs, target_dir, entries, SHELL_COMPLETION_ENTRIES, &count) != FAT16_OK) {
        return;
    }

    for (int i = 0; i < count; i++) {
        int is_dir = (entries[i].attr & FAT16_ATTR_DIRECTORY) != 0;
        if (dirs_only && !is_dir) {
            continue;
        }
        if (!starts_with_nocase(entries[i].name, name_prefix)) {
            continue;
        }

        char candidate[CONSOLE_COMPLETION_TEXT_MAX];
        uint32_t out_len = 0;
        candidate[0] = '\0';

        append_str(candidate, sizeof(candidate), &out_len, path_prefix);
        append_str(candidate, sizeof(candidate), &out_len, entries[i].name);

        if (candidate[0] != '\0') {
            completion_add(out, candidate);
        }
    }

    completion_sort(out);
}

void shell_run(fat16_fs_t* fs, int fs_ready) {
    char line[SHELL_LINE_MAX];

    cwd_cluster = 0;
    cwd_depth = 0;

    while (1) {
        build_prompt();

        completion_ctx_t completion_ctx;
        completion_ctx.fs = fs;
        completion_ctx.fs_ready = fs_ready;
        completion_ctx.cwd_cluster = cwd_cluster;
        completion_ctx.cwd_depth = cwd_depth;
        copy_parts(completion_ctx.cwd_parts, (const char (*)[SHELL_NAME_MAX])cwd_parts, cwd_depth);

        console_read_line_opts(
            line,
            sizeof(line),
            1,
            NULL,
            prompt_buf,
            shell_completion,
            &completion_ctx
        );

        char* input = skip_spaces(line);
        trim_end(input);

        if (*input == '\0') {
            continue;
        }

        char raw_input[SHELL_LINE_MAX];
        copy_limited(raw_input, input, sizeof(raw_input));

        char* parse = input;
        char* command = next_token(&parse);
        if (!command) {
            continue;
        }

        if (strcmp(command, "help") == 0) {
            shell_help();
            continue;
        }

        if (strcmp(command, "about") == 0) {
            if (next_token(&parse)) {
                vga_write_line("usage: about");
                continue;
            }
            shell_about();
            continue;
        }

        if (strcmp(command, "ilovelinux") == 0) {
            if (next_token(&parse)) {
                vga_write_line("usage: ilovelinux");
                continue;
            }
            creepy_mode();
            continue;
        }

        if (strcmp(command, "clear") == 0) {
            vga_clear();
            continue;
        }

        if (strcmp(command, "echo") == 0) {
            char* text = skip_spaces(parse);
            trim_end(text);
            if (*text) {
                vga_write_line(text);
            } else {
                vga_putc('\n');
            }
            continue;
        }

        if (strcmp(command, "poweroff") == 0) {
            if (next_token(&parse)) {
                vga_write_line("usage: poweroff");
                continue;
            }
            vga_write_line("Powering off...");
            poweroff();
            continue;
        }

        if (strcmp(command, "reboot") == 0) {
            if (next_token(&parse)) {
                vga_write_line("usage: reboot");
                continue;
            }
            vga_write_line("Rebooting...");
            reboot();
            continue;
        }

        if (!fs_ready) {
            vga_write_line("Filesystem unavailable");
            continue;
        }

        if (strcmp(command, "ls") == 0) {
            char* arg1 = next_token(&parse);
            char* arg2 = next_token(&parse);
            if (arg2) {
                vga_write_line("usage: ls [dir]");
                continue;
            }

            uint16_t target_cluster = cwd_cluster;
            if (arg1) {
                int rc = resolve_dir_path_with_base(
                    fs,
                    cwd_cluster,
                    (const char (*)[SHELL_NAME_MAX])cwd_parts,
                    cwd_depth,
                    arg1,
                    &target_cluster,
                    NULL,
                    NULL
                );
                if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                    print_label_target("Directory not found: ", arg1);
                    continue;
                }
                if (rc != FAT16_OK) {
                    print_generic_fs_error(rc);
                    continue;
                }
            }

            int rc = fat16_list_dir(fs, target_cluster);
            if (rc != FAT16_OK) {
                print_generic_fs_error(rc);
            }
            continue;
        }

        if (strcmp(command, "cd") == 0) {
            char* arg1 = next_token(&parse);
            char* arg2 = next_token(&parse);
            if (!arg1 || arg2) {
                vga_write_line("usage: cd <dir>");
                continue;
            }

            uint16_t new_cluster = cwd_cluster;
            char new_parts[SHELL_MAX_DEPTH][SHELL_NAME_MAX];
            uint32_t new_depth = cwd_depth;

            int rc = resolve_dir_path_with_base(
                fs,
                cwd_cluster,
                (const char (*)[SHELL_NAME_MAX])cwd_parts,
                cwd_depth,
                arg1,
                &new_cluster,
                new_parts,
                &new_depth
            );
            if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                print_label_target("Directory not found: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_generic_fs_error(rc);
                continue;
            }

            cwd_cluster = new_cluster;
            cwd_depth = new_depth;
            copy_parts(cwd_parts, (const char (*)[SHELL_NAME_MAX])new_parts, new_depth);
            continue;
        }

        if (strcmp(command, "open") == 0) {
            char* arg1 = next_token(&parse);
            char* arg2 = next_token(&parse);
            if (!arg1 || arg2) {
                vga_write_line("usage: open <file>");
                continue;
            }

            uint16_t dir_cluster = cwd_cluster;
            char file_name[SHELL_NAME_MAX];
            int rc = resolve_parent_and_name(fs, arg1, &dir_cluster, file_name);
            if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                print_label_target("Directory not found: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_label_target("Invalid file name: ", arg1);
                continue;
            }

            shell_open_file(fs, dir_cluster, file_name, arg1);
            continue;
        }

        if (strcmp(command, "edit") == 0) {
            char* arg1 = next_token(&parse);
            char* arg2 = next_token(&parse);
            char* arg3 = next_token(&parse);
            int force_override = 0;
            if (!arg1) {
                vga_write_line("usage: edit <file> [:w!]");
                continue;
            }
            if (arg2) {
                if (arg3 || strcmp(arg2, ":w!") != 0) {
                    vga_write_line("usage: edit <file> [:w!]");
                    continue;
                }
                force_override = 1;
            }

            if (is_protected_path(arg1) && !force_override) {
                vga_write_line("Protected file. Use :w! to override.");
                continue;
            }

            uint16_t dir_cluster = cwd_cluster;
            char file_name[SHELL_NAME_MAX];
            int rc = resolve_parent_and_name(fs, arg1, &dir_cluster, file_name);
            if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                print_label_target("Directory not found: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_label_target("Invalid file name: ", arg1);
                continue;
            }

            if (is_protected_path(arg1) && force_override) {
                backup_protected_file_if_needed(fs, arg1);
            }

            editor_open(fs, dir_cluster, file_name);
            continue;
        }

        if (strcmp(command, "touch") == 0) {
            char* arg1 = next_token(&parse);
            char* arg2 = next_token(&parse);
            if (!arg1 || arg2) {
                vga_write_line("usage: touch <file>");
                continue;
            }

            uint16_t dir_cluster = cwd_cluster;
            char file_name[SHELL_NAME_MAX];
            int rc = resolve_parent_and_name(fs, arg1, &dir_cluster, file_name);
            if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                print_label_target("Directory not found: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_label_target("Invalid file name: ", arg1);
                continue;
            }

            rc = fat16_touch(fs, dir_cluster, file_name);
            if (rc == FAT16_ERR_ISDIR) {
                print_label_target("Invalid destination: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_generic_fs_error(rc);
            }
            continue;
        }

        if (strcmp(command, "mkdir") == 0) {
            char* arg1 = next_token(&parse);
            char* arg2 = next_token(&parse);
            if (!arg1 || arg2) {
                vga_write_line("usage: mkdir <dir>");
                continue;
            }

            uint16_t parent_cluster = cwd_cluster;
            char dir_name[SHELL_NAME_MAX];
            int rc = resolve_parent_and_name(fs, arg1, &parent_cluster, dir_name);
            if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                print_label_target("Directory not found: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_label_target("Invalid directory name: ", arg1);
                continue;
            }

            rc = fat16_mkdir(fs, parent_cluster, dir_name);
            if (rc == FAT16_ERR_EXISTS) {
                print_label_target("Already exists: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_generic_fs_error(rc);
            }
            continue;
        }

        if (strcmp(command, "rm") == 0) {
            char* arg1 = next_token(&parse);
            char* arg2 = next_token(&parse);
            if (!arg1 || arg2) {
                vga_write_line("usage: rm <name>");
                continue;
            }

            uint16_t dir_cluster = cwd_cluster;
            char name[SHELL_NAME_MAX];
            int rc = resolve_parent_and_name(fs, arg1, &dir_cluster, name);
            if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                print_label_target("Directory not found: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_label_target("Invalid target: ", arg1);
                continue;
            }

            if (!confirm_delete()) {
                continue;
            }

            rc = fat16_remove(fs, dir_cluster, name);
            if (rc == FAT16_ERR_NOT_FOUND) {
                print_label_target("File not found: ", arg1);
                continue;
            }
            if (rc == FAT16_ERR_NOTEMPTY) {
                print_label_target("Directory not empty: ", arg1);
                continue;
            }
            if (rc != FAT16_OK) {
                print_generic_fs_error(rc);
            }
            continue;
        }

        if (strcmp(command, "cp") == 0 || strcmp(command, "mv") == 0) {
            char* src_arg = next_token(&parse);
            char* dst_arg = next_token(&parse);
            char* extra = next_token(&parse);
            if (!src_arg || !dst_arg || extra) {
                if (strcmp(command, "cp") == 0) {
                    vga_write_line("usage: cp <source> <destination>");
                } else {
                    vga_write_line("usage: mv <source> <destination>");
                }
                continue;
            }

            uint16_t src_dir_cluster = cwd_cluster;
            char src_name[SHELL_NAME_MAX];
            int rc = resolve_parent_and_name(fs, src_arg, &src_dir_cluster, src_name);
            if (rc == FAT16_ERR_NOT_FOUND || rc == FAT16_ERR_NOTDIR) {
                print_label_target("Directory not found: ", src_arg);
                continue;
            }
            if (rc != FAT16_OK) {
                print_label_target("Invalid source: ", src_arg);
                continue;
            }

            fat16_dirent_t src_entry;
            rc = fat16_stat(fs, src_dir_cluster, src_name, &src_entry);
            if (rc == FAT16_ERR_NOT_FOUND) {
                print_label_target("File not found: ", src_arg);
                continue;
            }
            if (rc != FAT16_OK) {
                print_generic_fs_error(rc);
                continue;
            }
            if (src_entry.attr & FAT16_ATTR_DIRECTORY) {
                print_label_target("Invalid source: ", src_arg);
                continue;
            }

            uint16_t dst_dir_cluster = cwd_cluster;
            char dst_name[SHELL_NAME_MAX];

            rc = resolve_dir_path_with_base(
                fs,
                cwd_cluster,
                (const char (*)[SHELL_NAME_MAX])cwd_parts,
                cwd_depth,
                dst_arg,
                &dst_dir_cluster,
                NULL,
                NULL
            );
            if (rc == FAT16_OK) {
                copy_limited(dst_name, src_entry.name, sizeof(dst_name));
            } else {
                rc = resolve_parent_and_name(fs, dst_arg, &dst_dir_cluster, dst_name);
                if (rc != FAT16_OK) {
                    print_label_target("Invalid destination: ", dst_arg);
                    continue;
                }
            }

            if (strcmp(command, "cp") == 0 &&
                src_dir_cluster == dst_dir_cluster &&
                strcmp(src_name, dst_name) == 0) {
                continue;
            }

            if (strcmp(command, "cp") == 0) {
                rc = fat16_copy_file(fs, src_dir_cluster, src_name, dst_dir_cluster, dst_name);
            } else {
                rc = fat16_move_file(fs, src_dir_cluster, src_name, dst_dir_cluster, dst_name);
            }

            if (rc == FAT16_ERR_NOT_FOUND) {
                print_label_target("File not found: ", src_arg);
                continue;
            }
            if (rc == FAT16_ERR_EXISTS || rc == FAT16_ERR_INVALID || rc == FAT16_ERR_NOTDIR || rc == FAT16_ERR_ISDIR) {
                print_label_target("Invalid destination: ", dst_arg);
                continue;
            }
            if (rc != FAT16_OK) {
                print_generic_fs_error(rc);
            }
            continue;
        }

        vga_write("Unknown command: ");
        vga_write(raw_input);
        vga_putc('\n');

        const char* maybe = suggest_command(command);
        if (maybe) {
            vga_write("Did you mean: ");
            vga_write(maybe);
            vga_write_line("?");
        }
    }
}
