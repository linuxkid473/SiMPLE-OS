#include "fat16.h"
#include "ata.h"
#include "string.h"
#include "vga.h"

#define FAT16_EOC 0xFFF8
#define FAT16_ATTR_LFN 0x0F
#define FAT16_LS_MAX_ENTRIES 256

typedef struct __attribute__((packed)) {
    uint8_t bootstrap[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} fat16_bpb_t;

typedef struct __attribute__((packed)) {
    uint8_t name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t create_time_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} fat16_raw_dirent_t;

typedef struct {
    uint32_t lba;
    uint16_t offset;
    int valid;
} dir_slot_t;

static uint8_t sector[512];

static uint32_t cluster_to_lba(const fat16_fs_t* fs, uint16_t cluster) {
    return fs->data_lba + ((uint32_t)(cluster - 2U) * fs->sectors_per_cluster);
}

static int read_sector(uint32_t lba, uint8_t* buffer) {
    return ata_read_sector(lba, buffer) == 0 ? FAT16_OK : FAT16_ERR_IO;
}

static int write_sector(uint32_t lba, const uint8_t* buffer) {
    return ata_write_sector(lba, buffer) == 0 ? FAT16_OK : FAT16_ERR_IO;
}

static uint16_t read_fat_entry(fat16_fs_t* fs, uint16_t cluster) {
    uint32_t offset = (uint32_t)cluster * 2U;
    uint32_t fat_lba = fs->fat_lba + (offset / fs->bytes_per_sector);
    uint16_t off = (uint16_t)(offset % fs->bytes_per_sector);

    if (read_sector(fat_lba, sector) != FAT16_OK) {
        return FAT16_EOC;
    }

    return (uint16_t)(sector[off] | ((uint16_t)sector[off + 1] << 8));
}

static int write_fat_entry(fat16_fs_t* fs, uint16_t cluster, uint16_t value) {
    uint32_t offset = (uint32_t)cluster * 2U;
    uint32_t fat_sector = offset / fs->bytes_per_sector;
    uint16_t off = (uint16_t)(offset % fs->bytes_per_sector);

    for (uint8_t i = 0; i < fs->fat_count; i++) {
        uint32_t lba = fs->fat_lba + (uint32_t)i * fs->sectors_per_fat + fat_sector;
        if (read_sector(lba, sector) != FAT16_OK) {
            return FAT16_ERR_IO;
        }

        sector[off] = (uint8_t)(value & 0xFF);
        sector[off + 1] = (uint8_t)((value >> 8) & 0xFF);

        if (write_sector(lba, sector) != FAT16_OK) {
            return FAT16_ERR_IO;
        }
    }

    return FAT16_OK;
}

static int clear_cluster(fat16_fs_t* fs, uint16_t cluster) {
    memset(sector, 0, sizeof(sector));
    uint32_t base_lba = cluster_to_lba(fs, cluster);

    for (uint8_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (write_sector(base_lba + i, sector) != FAT16_OK) {
            return FAT16_ERR_IO;
        }
    }

    return FAT16_OK;
}

static uint16_t alloc_cluster(fat16_fs_t* fs) {
    for (uint16_t cluster = 2; cluster <= fs->max_clusters; cluster++) {
        if (read_fat_entry(fs, cluster) == 0x0000) {
            if (write_fat_entry(fs, cluster, 0xFFFF) != FAT16_OK) {
                return 0;
            }
            if (clear_cluster(fs, cluster) != FAT16_OK) {
                write_fat_entry(fs, cluster, 0x0000);
                return 0;
            }
            return cluster;
        }
    }

    return 0;
}

static void free_cluster_chain(fat16_fs_t* fs, uint16_t start_cluster) {
    uint16_t cluster = start_cluster;

    while (cluster >= 2 && cluster < FAT16_EOC) {
        uint16_t next = read_fat_entry(fs, cluster);
        write_fat_entry(fs, cluster, 0x0000);

        if (next == cluster) {
            break;
        }
        cluster = next;
    }
}

static int is_lfn_entry(const fat16_raw_dirent_t* entry) {
    return (entry->attr & FAT16_ATTR_LFN) == FAT16_ATTR_LFN;
}

static int fat_name_to_83(const char* name, uint8_t out[11]) {
    memset(out, ' ', 11);

    if (strcmp(name, ".") == 0) {
        out[0] = '.';
        return FAT16_OK;
    }

    if (strcmp(name, "..") == 0) {
        out[0] = '.';
        out[1] = '.';
        return FAT16_OK;
    }

    const char* dot = strchr(name, '.');
    size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
    size_t ext_len = 0;

    if (base_len == 0 || base_len > 8) {
        return FAT16_ERR_INVALID;
    }

    if (dot) {
        ext_len = strlen(dot + 1);
        if (ext_len > 3) {
            return FAT16_ERR_INVALID;
        }
    }

    for (size_t i = 0; i < base_len; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - ('a' - 'A'));
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return FAT16_ERR_INVALID;
        }
        out[i] = (uint8_t)c;
    }

    if (dot) {
        for (size_t i = 0; i < ext_len; i++) {
            char c = dot[1 + i];
            if (c >= 'a' && c <= 'z') {
                c = (char)(c - ('a' - 'A'));
            }
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
                return FAT16_ERR_INVALID;
            }
            out[8 + i] = (uint8_t)c;
        }
    }

    return FAT16_OK;
}

static void fat_name_from_83(const uint8_t in[11], char out[13]) {
    int pos = 0;

    for (int i = 0; i < 8 && in[i] != ' '; i++) {
        char c = (char)in[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + ('a' - 'A'));
        }
        out[pos++] = c;
    }

    if (in[8] != ' ') {
        out[pos++] = '.';
        for (int i = 8; i < 11 && in[i] != ' '; i++) {
            char c = (char)in[i];
            if (c >= 'A' && c <= 'Z') {
                c = (char)(c + ('a' - 'A'));
            }
            out[pos++] = c;
        }
    }

    out[pos] = '\0';
}

static int write_dir_entry_at(uint32_t lba, uint16_t offset, const fat16_raw_dirent_t* entry) {
    if (read_sector(lba, sector) != FAT16_OK) {
        return FAT16_ERR_IO;
    }
    memcpy(sector + offset, entry, sizeof(fat16_raw_dirent_t));
    return write_sector(lba, sector);
}

static int find_entry_in_dir(
    fat16_fs_t* fs,
    uint16_t dir_cluster,
    const uint8_t name83[11],
    fat16_raw_dirent_t* out_entry,
    dir_slot_t* out_slot
) {
    if (dir_cluster == 0) {
        for (uint16_t s = 0; s < fs->root_sectors; s++) {
            uint32_t lba = fs->root_lba + s;
            if (read_sector(lba, sector) != FAT16_OK) {
                return FAT16_ERR_IO;
            }

            for (uint16_t off = 0; off < 512; off += 32) {
                fat16_raw_dirent_t* entry = (fat16_raw_dirent_t*)(sector + off);

                if (entry->name[0] == 0x00) {
                    return FAT16_ERR_NOT_FOUND;
                }
                if (entry->name[0] == 0xE5 || is_lfn_entry(entry)) {
                    continue;
                }
                if (memcmp(entry->name, name83, 11) == 0) {
                    memcpy(out_entry, entry, sizeof(fat16_raw_dirent_t));
                    out_slot->lba = lba;
                    out_slot->offset = off;
                    out_slot->valid = 1;
                    return FAT16_OK;
                }
            }
        }
        return FAT16_ERR_NOT_FOUND;
    }

    uint16_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT16_EOC) {
        uint32_t base_lba = cluster_to_lba(fs, cluster);

        for (uint8_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = base_lba + s;
            if (read_sector(lba, sector) != FAT16_OK) {
                return FAT16_ERR_IO;
            }

            for (uint16_t off = 0; off < 512; off += 32) {
                fat16_raw_dirent_t* entry = (fat16_raw_dirent_t*)(sector + off);

                if (entry->name[0] == 0x00) {
                    return FAT16_ERR_NOT_FOUND;
                }
                if (entry->name[0] == 0xE5 || is_lfn_entry(entry)) {
                    continue;
                }
                if (memcmp(entry->name, name83, 11) == 0) {
                    memcpy(out_entry, entry, sizeof(fat16_raw_dirent_t));
                    out_slot->lba = lba;
                    out_slot->offset = off;
                    out_slot->valid = 1;
                    return FAT16_OK;
                }
            }
        }

        uint16_t next = read_fat_entry(fs, cluster);
        if (next == cluster) {
            break;
        }
        cluster = next;
    }

    return FAT16_ERR_NOT_FOUND;
}

static int find_free_slot(fat16_fs_t* fs, uint16_t dir_cluster, dir_slot_t* out_slot) {
    out_slot->valid = 0;

    if (dir_cluster == 0) {
        for (uint16_t s = 0; s < fs->root_sectors; s++) {
            uint32_t lba = fs->root_lba + s;
            if (read_sector(lba, sector) != FAT16_OK) {
                return FAT16_ERR_IO;
            }

            for (uint16_t off = 0; off < 512; off += 32) {
                fat16_raw_dirent_t* entry = (fat16_raw_dirent_t*)(sector + off);
                if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                    out_slot->lba = lba;
                    out_slot->offset = off;
                    out_slot->valid = 1;
                    return FAT16_OK;
                }
            }
        }
        return FAT16_ERR_NOSPACE;
    }

    uint16_t cluster = dir_cluster;
    uint16_t last_cluster = dir_cluster;

    while (cluster >= 2 && cluster < FAT16_EOC) {
        last_cluster = cluster;
        uint32_t base_lba = cluster_to_lba(fs, cluster);

        for (uint8_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = base_lba + s;
            if (read_sector(lba, sector) != FAT16_OK) {
                return FAT16_ERR_IO;
            }

            for (uint16_t off = 0; off < 512; off += 32) {
                fat16_raw_dirent_t* entry = (fat16_raw_dirent_t*)(sector + off);
                if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                    out_slot->lba = lba;
                    out_slot->offset = off;
                    out_slot->valid = 1;
                    return FAT16_OK;
                }
            }
        }

        uint16_t next = read_fat_entry(fs, cluster);
        if (next == cluster || next >= FAT16_EOC) {
            break;
        }
        cluster = next;
    }

    uint16_t new_cluster = alloc_cluster(fs);
    if (new_cluster == 0) {
        return FAT16_ERR_NOSPACE;
    }

    if (write_fat_entry(fs, last_cluster, new_cluster) != FAT16_OK ||
        write_fat_entry(fs, new_cluster, 0xFFFF) != FAT16_OK) {
        write_fat_entry(fs, new_cluster, 0x0000);
        return FAT16_ERR_IO;
    }

    out_slot->lba = cluster_to_lba(fs, new_cluster);
    out_slot->offset = 0;
    out_slot->valid = 1;
    return FAT16_OK;
}

static int is_dot_or_dotdot_entry(const fat16_raw_dirent_t* entry) {
    if (entry->name[0] == '.' && entry->name[1] == ' ') {
        return 1;
    }
    if (entry->name[0] == '.' && entry->name[1] == '.' && entry->name[2] == ' ') {
        return 1;
    }
    return 0;
}

static int dir_is_empty(fat16_fs_t* fs, uint16_t dir_cluster) {
    if (dir_cluster < 2) {
        return 0;
    }

    uint16_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT16_EOC) {
        uint32_t base_lba = cluster_to_lba(fs, cluster);

        for (uint8_t s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t lba = base_lba + s;
            if (read_sector(lba, sector) != FAT16_OK) {
                return 0;
            }

            for (uint16_t off = 0; off < 512; off += 32) {
                fat16_raw_dirent_t* entry = (fat16_raw_dirent_t*)(sector + off);
                if (entry->name[0] == 0x00) {
                    return 1;
                }
                if (entry->name[0] == 0xE5 || is_lfn_entry(entry) || (entry->attr & 0x08)) {
                    continue;
                }
                if (is_dot_or_dotdot_entry(entry)) {
                    continue;
                }
                return 0;
            }
        }

        uint16_t next = read_fat_entry(fs, cluster);
        if (next == cluster || next >= FAT16_EOC) {
            break;
        }
        cluster = next;
    }

    return 1;
}

static void print_u32(uint32_t value) {
    char digits[11];
    int i = 0;

    if (value == 0) {
        vga_putc('0');
        return;
    }

    while (value > 0 && i < 10) {
        digits[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        vga_putc(digits[--i]);
    }
}

static int ls_entry_compare(const fat16_dirent_t* a, const fat16_dirent_t* b) {
    int a_dir = (a->attr & FAT16_ATTR_DIRECTORY) != 0;
    int b_dir = (b->attr & FAT16_ATTR_DIRECTORY) != 0;

    if (a_dir != b_dir) {
        return a_dir ? -1 : 1;
    }

    return strcmp(a->name, b->name);
}

static void ls_sort(fat16_dirent_t* entries, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (ls_entry_compare(&entries[i], &entries[j]) > 0) {
                fat16_dirent_t tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
}

int fat16_mount(fat16_fs_t* fs) {
    if (read_sector(0, sector) != FAT16_OK) {
        return FAT16_ERR_IO;
    }

    uint32_t partition_lba = 0;

    for (int i = 0; i < 4; i++) {
        uint8_t* p = sector + 446 + i * 16;
        uint8_t type = p[4];
        if (type == 0x04 || type == 0x06 || type == 0x0E) {
            partition_lba = (uint32_t)(p[8] | (p[9] << 8) | (p[10] << 16) | (p[11] << 24));
            break;
        }
    }

    if (read_sector(partition_lba, sector) != FAT16_OK) {
        return FAT16_ERR_IO;
    }

    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return FAT16_ERR_INVALID;
    }

    fat16_bpb_t* bpb = (fat16_bpb_t*)sector;

    if (bpb->bytes_per_sector != 512 || bpb->sectors_per_fat == 0 || bpb->sectors_per_cluster == 0) {
        return FAT16_ERR_INVALID;
    }

    uint32_t total_sectors = bpb->total_sectors_16 ? bpb->total_sectors_16 : bpb->total_sectors_32;
    uint16_t root_sectors = (uint16_t)(((uint32_t)bpb->root_entries * 32U + (bpb->bytes_per_sector - 1U)) / bpb->bytes_per_sector);

    fs->partition_lba = partition_lba;
    fs->bytes_per_sector = bpb->bytes_per_sector;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->reserved_sectors = bpb->reserved_sectors;
    fs->fat_count = bpb->fat_count;
    fs->root_entries = bpb->root_entries;
    fs->sectors_per_fat = bpb->sectors_per_fat;
    fs->root_sectors = root_sectors;
    fs->total_sectors = total_sectors;

    fs->fat_lba = partition_lba + bpb->reserved_sectors;
    fs->root_lba = fs->fat_lba + (uint32_t)bpb->fat_count * bpb->sectors_per_fat;
    fs->data_lba = fs->root_lba + root_sectors;

    uint32_t data_sectors = total_sectors -
        (bpb->reserved_sectors + (uint32_t)bpb->fat_count * bpb->sectors_per_fat + root_sectors);
    uint32_t cluster_count = data_sectors / bpb->sectors_per_cluster;

    fs->max_clusters = (uint32_t)(cluster_count + 1U);

    return FAT16_OK;
}

int fat16_list_entries(fat16_fs_t* fs, uint16_t dir_cluster, fat16_dirent_t* entries, int max_entries, int* out_count) {
    if (!entries || max_entries <= 0 || !out_count) {
        return FAT16_ERR_INVALID;
    }

    int count = 0;

    if (dir_cluster == 0) {
        for (uint16_t s = 0; s < fs->root_sectors; s++) {
            uint32_t lba = fs->root_lba + s;
            if (read_sector(lba, sector) != FAT16_OK) {
                return FAT16_ERR_IO;
            }

            for (uint16_t off = 0; off < 512; off += 32) {
                fat16_raw_dirent_t* entry = (fat16_raw_dirent_t*)(sector + off);
                if (entry->name[0] == 0x00) {
                    goto done_collect;
                }
                if (entry->name[0] == 0xE5 || is_lfn_entry(entry) || (entry->attr & 0x08)) {
                    continue;
                }
                if (is_dot_or_dotdot_entry(entry)) {
                    continue;
                }

                if (count < max_entries) {
                    fat_name_from_83(entry->name, entries[count].name);
                    entries[count].attr = entry->attr;
                    entries[count].first_cluster = entry->first_cluster_low;
                    entries[count].size = entry->file_size;
                    count++;
                }
            }
        }
    } else {
        uint16_t cluster = dir_cluster;
        while (cluster >= 2 && cluster < FAT16_EOC) {
            uint32_t base_lba = cluster_to_lba(fs, cluster);

            for (uint8_t s = 0; s < fs->sectors_per_cluster; s++) {
                uint32_t lba = base_lba + s;
                if (read_sector(lba, sector) != FAT16_OK) {
                    return FAT16_ERR_IO;
                }

                for (uint16_t off = 0; off < 512; off += 32) {
                    fat16_raw_dirent_t* entry = (fat16_raw_dirent_t*)(sector + off);
                    if (entry->name[0] == 0x00) {
                        goto done_collect;
                    }
                    if (entry->name[0] == 0xE5 || is_lfn_entry(entry) || (entry->attr & 0x08)) {
                        continue;
                    }
                    if (is_dot_or_dotdot_entry(entry)) {
                        continue;
                    }

                    if (count < max_entries) {
                        fat_name_from_83(entry->name, entries[count].name);
                        entries[count].attr = entry->attr;
                        entries[count].first_cluster = entry->first_cluster_low;
                        entries[count].size = entry->file_size;
                        count++;
                    }
                }
            }

            uint16_t next = read_fat_entry(fs, cluster);
            if (next == cluster || next >= FAT16_EOC) {
                break;
            }
            cluster = next;
        }
    }

done_collect:
    *out_count = count;
    return FAT16_OK;
}

int fat16_list_dir(fat16_fs_t* fs, uint16_t dir_cluster) {
    fat16_dirent_t entries[FAT16_LS_MAX_ENTRIES];
    int count = 0;

    int rc = fat16_list_entries(fs, dir_cluster, entries, FAT16_LS_MAX_ENTRIES, &count);
    if (rc != FAT16_OK) {
        return rc;
    }

    if (count == 0) {
        vga_write_line("(empty)");
        return FAT16_OK;
    }

    ls_sort(entries, count);

    for (int i = 0; i < count; i++) {
        if (entries[i].attr & FAT16_ATTR_DIRECTORY) {
            vga_write("[DIR]  ");
            vga_write(entries[i].name);
            vga_putc('\n');
        } else {
            vga_write("[FILE] ");
            vga_write(entries[i].name);
            vga_write("  size=");
            print_u32(entries[i].size);
            vga_putc('\n');
        }
    }

    return FAT16_OK;
}

int fat16_stat(fat16_fs_t* fs, uint16_t dir_cluster, const char* name, fat16_dirent_t* out_entry) {
    if (!out_entry) {
        return FAT16_ERR_INVALID;
    }

    uint8_t name83[11];
    int rc = fat_name_to_83(name, name83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t entry;
    dir_slot_t slot;
    rc = find_entry_in_dir(fs, dir_cluster, name83, &entry, &slot);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat_name_from_83(entry.name, out_entry->name);
    out_entry->attr = entry.attr;
    out_entry->first_cluster = entry.first_cluster_low;
    out_entry->size = entry.file_size;
    return FAT16_OK;
}

static int mark_entry_deleted(uint32_t lba, uint16_t offset) {
    if (read_sector(lba, sector) != FAT16_OK) {
        return FAT16_ERR_IO;
    }
    sector[offset] = 0xE5;
    memset(sector + offset + 1, 0, 31);
    return write_sector(lba, sector);
}

static int allocate_chain(fat16_fs_t* fs, uint32_t file_size, uint16_t* out_first_cluster) {
    if (file_size == 0) {
        *out_first_cluster = 0;
        return FAT16_OK;
    }

    uint32_t cluster_bytes = (uint32_t)fs->sectors_per_cluster * 512U;
    uint32_t needed = (file_size + cluster_bytes - 1U) / cluster_bytes;
    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    for (uint32_t i = 0; i < needed; i++) {
        uint16_t c = alloc_cluster(fs);
        if (c == 0) {
            if (first_cluster != 0) {
                free_cluster_chain(fs, first_cluster);
            }
            return FAT16_ERR_NOSPACE;
        }

        if (prev_cluster != 0) {
            if (write_fat_entry(fs, prev_cluster, c) != FAT16_OK) {
                free_cluster_chain(fs, first_cluster);
                return FAT16_ERR_IO;
            }
        } else {
            first_cluster = c;
        }
        prev_cluster = c;
    }

    if (write_fat_entry(fs, prev_cluster, 0xFFFF) != FAT16_OK) {
        free_cluster_chain(fs, first_cluster);
        return FAT16_ERR_IO;
    }

    *out_first_cluster = first_cluster;
    return FAT16_OK;
}

int fat16_change_dir(fat16_fs_t* fs, uint16_t dir_cluster, const char* name, uint16_t* out_cluster) {
    if (strcmp(name, "/") == 0) {
        *out_cluster = 0;
        return FAT16_OK;
    }

    if (strcmp(name, ".") == 0) {
        *out_cluster = dir_cluster;
        return FAT16_OK;
    }

    if (strcmp(name, "..") == 0) {
        if (dir_cluster == 0) {
            *out_cluster = 0;
            return FAT16_OK;
        }

        uint8_t dotdot[11];
        fat_name_to_83("..", dotdot);
        fat16_raw_dirent_t entry;
        dir_slot_t slot;

        int rc = find_entry_in_dir(fs, dir_cluster, dotdot, &entry, &slot);
        if (rc != FAT16_OK) {
            return rc;
        }

        *out_cluster = entry.first_cluster_low;
        return FAT16_OK;
    }

    uint8_t name83[11];
    int rc = fat_name_to_83(name, name83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t entry;
    dir_slot_t slot;
    rc = find_entry_in_dir(fs, dir_cluster, name83, &entry, &slot);
    if (rc != FAT16_OK) {
        return rc;
    }

    if ((entry.attr & FAT16_ATTR_DIRECTORY) == 0) {
        return FAT16_ERR_NOTDIR;
    }

    *out_cluster = entry.first_cluster_low;
    return FAT16_OK;
}

int fat16_touch(fat16_fs_t* fs, uint16_t dir_cluster, const char* name) {
    uint8_t name83[11];
    int rc = fat_name_to_83(name, name83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t entry;
    dir_slot_t existing;

    rc = find_entry_in_dir(fs, dir_cluster, name83, &entry, &existing);
    if (rc == FAT16_OK) {
        if (entry.attr & FAT16_ATTR_DIRECTORY) {
            return FAT16_ERR_ISDIR;
        }
        return FAT16_OK;
    }
    if (rc != FAT16_ERR_NOT_FOUND) {
        return rc;
    }

    dir_slot_t slot;
    rc = find_free_slot(fs, dir_cluster, &slot);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    memcpy(new_entry.name, name83, 11);
    new_entry.attr = 0x20;
    new_entry.first_cluster_low = 0;
    new_entry.file_size = 0;

    return write_dir_entry_at(slot.lba, slot.offset, &new_entry);
}

int fat16_mkdir(fat16_fs_t* fs, uint16_t dir_cluster, const char* name) {
    uint8_t name83[11];
    int rc = fat_name_to_83(name, name83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t entry;
    dir_slot_t existing;
    rc = find_entry_in_dir(fs, dir_cluster, name83, &entry, &existing);
    if (rc == FAT16_OK) {
        return FAT16_ERR_EXISTS;
    }
    if (rc != FAT16_ERR_NOT_FOUND) {
        return rc;
    }

    dir_slot_t slot;
    rc = find_free_slot(fs, dir_cluster, &slot);
    if (rc != FAT16_OK) {
        return rc;
    }

    uint16_t new_cluster = alloc_cluster(fs);
    if (new_cluster == 0) {
        return FAT16_ERR_NOSPACE;
    }

    fat16_raw_dirent_t new_dir;
    memset(&new_dir, 0, sizeof(new_dir));
    memcpy(new_dir.name, name83, 11);
    new_dir.attr = FAT16_ATTR_DIRECTORY;
    new_dir.first_cluster_low = new_cluster;
    new_dir.file_size = 0;

    rc = write_dir_entry_at(slot.lba, slot.offset, &new_dir);
    if (rc != FAT16_OK) {
        write_fat_entry(fs, new_cluster, 0x0000);
        return rc;
    }

    uint32_t dir_lba = cluster_to_lba(fs, new_cluster);
    if (read_sector(dir_lba, sector) != FAT16_OK) {
        return FAT16_ERR_IO;
    }
    memset(sector, 0, sizeof(sector));

    fat16_raw_dirent_t dot;
    memset(&dot, 0, sizeof(dot));
    memset(dot.name, ' ', 11);
    dot.name[0] = '.';
    dot.attr = FAT16_ATTR_DIRECTORY;
    dot.first_cluster_low = new_cluster;

    fat16_raw_dirent_t dotdot;
    memset(&dotdot, 0, sizeof(dotdot));
    memset(dotdot.name, ' ', 11);
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';
    dotdot.attr = FAT16_ATTR_DIRECTORY;
    dotdot.first_cluster_low = dir_cluster;

    memcpy(sector, &dot, sizeof(dot));
    memcpy(sector + 32, &dotdot, sizeof(dotdot));

    if (write_sector(dir_lba, sector) != FAT16_OK) {
        return FAT16_ERR_IO;
    }

    return FAT16_OK;
}

int fat16_read_file(
    fat16_fs_t* fs,
    uint16_t dir_cluster,
    const char* name,
    char* out,
    uint32_t max_len,
    uint32_t* out_len
) {
    if (max_len == 0) {
        return FAT16_ERR_INVALID;
    }

    uint8_t name83[11];
    int rc = fat_name_to_83(name, name83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t entry;
    dir_slot_t slot;
    rc = find_entry_in_dir(fs, dir_cluster, name83, &entry, &slot);
    if (rc != FAT16_OK) {
        return rc;
    }

    if (entry.attr & FAT16_ATTR_DIRECTORY) {
        return FAT16_ERR_ISDIR;
    }

    uint32_t remaining = entry.file_size;
    uint32_t copied = 0;
    uint32_t limit = max_len - 1;

    if (remaining == 0 || entry.first_cluster_low == 0) {
        out[0] = '\0';
        if (out_len) {
            *out_len = 0;
        }
        return FAT16_OK;
    }

    uint16_t cluster = entry.first_cluster_low;

    while (cluster >= 2 && cluster < FAT16_EOC && remaining > 0) {
        uint32_t base_lba = cluster_to_lba(fs, cluster);

        for (uint8_t s = 0; s < fs->sectors_per_cluster && remaining > 0; s++) {
            if (read_sector(base_lba + s, sector) != FAT16_OK) {
                return FAT16_ERR_IO;
            }

            uint32_t take = remaining > 512 ? 512 : remaining;
            uint32_t room = (copied < limit) ? (limit - copied) : 0;
            uint32_t write_now = take < room ? take : room;

            if (write_now > 0) {
                memcpy(out + copied, sector, write_now);
                copied += write_now;
            }

            remaining -= take;
        }

        uint16_t next = read_fat_entry(fs, cluster);
        if (next == cluster) {
            break;
        }
        cluster = next;
    }

    out[copied] = '\0';
    if (out_len) {
        *out_len = copied;
    }

    return FAT16_OK;
}

int fat16_write_file(fat16_fs_t* fs, uint16_t dir_cluster, const char* name, const char* data, uint32_t len) {
    uint8_t name83[11];
    int rc = fat_name_to_83(name, name83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t entry;
    dir_slot_t slot;
    int found = find_entry_in_dir(fs, dir_cluster, name83, &entry, &slot);

    if (found == FAT16_OK) {
        if (entry.attr & FAT16_ATTR_DIRECTORY) {
            return FAT16_ERR_ISDIR;
        }
        if (entry.first_cluster_low != 0) {
            free_cluster_chain(fs, entry.first_cluster_low);
        }
    } else if (found == FAT16_ERR_NOT_FOUND) {
        rc = find_free_slot(fs, dir_cluster, &slot);
        if (rc != FAT16_OK) {
            return rc;
        }
        memset(&entry, 0, sizeof(entry));
        memcpy(entry.name, name83, 11);
        entry.attr = 0x20;
    } else {
        return found;
    }

    entry.first_cluster_low = 0;
    entry.file_size = len;

    if (len == 0) {
        return write_dir_entry_at(slot.lba, slot.offset, &entry);
    }

    uint32_t cluster_bytes = (uint32_t)fs->sectors_per_cluster * 512U;
    uint32_t needed = (len + cluster_bytes - 1U) / cluster_bytes;

    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    for (uint32_t i = 0; i < needed; i++) {
        uint16_t c = alloc_cluster(fs);
        if (c == 0) {
            if (first_cluster != 0) {
                free_cluster_chain(fs, first_cluster);
            }
            return FAT16_ERR_NOSPACE;
        }

        if (prev_cluster != 0) {
            write_fat_entry(fs, prev_cluster, c);
        } else {
            first_cluster = c;
        }

        prev_cluster = c;
    }

    write_fat_entry(fs, prev_cluster, 0xFFFF);

    uint32_t remaining = len;
    uint32_t offset = 0;
    uint16_t cluster = first_cluster;

    while (cluster >= 2 && cluster < FAT16_EOC && remaining > 0) {
        uint32_t base_lba = cluster_to_lba(fs, cluster);

        for (uint8_t s = 0; s < fs->sectors_per_cluster && remaining > 0; s++) {
            memset(sector, 0, sizeof(sector));
            uint32_t take = remaining > 512 ? 512 : remaining;
            memcpy(sector, data + offset, take);

            if (write_sector(base_lba + s, sector) != FAT16_OK) {
                free_cluster_chain(fs, first_cluster);
                return FAT16_ERR_IO;
            }

            offset += take;
            remaining -= take;
        }

        if (remaining == 0) {
            break;
        }

        uint16_t next = read_fat_entry(fs, cluster);
        if (next == cluster) {
            break;
        }
        cluster = next;
    }

    entry.first_cluster_low = first_cluster;
    return write_dir_entry_at(slot.lba, slot.offset, &entry);
}

int fat16_copy_file(fat16_fs_t* fs, uint16_t src_dir_cluster, const char* src_name, uint16_t dst_dir_cluster, const char* dst_name) {
    uint8_t src83[11];
    uint8_t dst83[11];
    int rc = fat_name_to_83(src_name, src83);
    if (rc != FAT16_OK) {
        return rc;
    }
    rc = fat_name_to_83(dst_name, dst83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t src_entry;
    dir_slot_t src_slot;
    rc = find_entry_in_dir(fs, src_dir_cluster, src83, &src_entry, &src_slot);
    if (rc != FAT16_OK) {
        return rc;
    }
    if (src_entry.attr & FAT16_ATTR_DIRECTORY) {
        return FAT16_ERR_ISDIR;
    }

    fat16_raw_dirent_t dst_entry;
    dir_slot_t dst_slot;
    rc = find_entry_in_dir(fs, dst_dir_cluster, dst83, &dst_entry, &dst_slot);
    if (rc == FAT16_OK) {
        if (dst_entry.attr & FAT16_ATTR_DIRECTORY) {
            return FAT16_ERR_ISDIR;
        }
        if (dst_entry.first_cluster_low >= 2) {
            free_cluster_chain(fs, dst_entry.first_cluster_low);
        }
    } else if (rc == FAT16_ERR_NOT_FOUND) {
        rc = find_free_slot(fs, dst_dir_cluster, &dst_slot);
        if (rc != FAT16_OK) {
            return rc;
        }
        memset(&dst_entry, 0, sizeof(dst_entry));
        memcpy(dst_entry.name, dst83, 11);
        dst_entry.attr = 0x20;
    } else {
        return rc;
    }

    dst_entry.first_cluster_low = 0;
    dst_entry.file_size = src_entry.file_size;

    if (src_entry.file_size == 0 || src_entry.first_cluster_low == 0) {
        return write_dir_entry_at(dst_slot.lba, dst_slot.offset, &dst_entry);
    }

    uint16_t dst_first_cluster = 0;
    rc = allocate_chain(fs, src_entry.file_size, &dst_first_cluster);
    if (rc != FAT16_OK) {
        return rc;
    }

    uint32_t remaining = src_entry.file_size;
    uint16_t src_cluster = src_entry.first_cluster_low;
    uint16_t dst_cluster = dst_first_cluster;

    while (remaining > 0 && src_cluster >= 2 && src_cluster < FAT16_EOC &&
           dst_cluster >= 2 && dst_cluster < FAT16_EOC) {
        uint32_t src_lba = cluster_to_lba(fs, src_cluster);
        uint32_t dst_lba = cluster_to_lba(fs, dst_cluster);

        for (uint8_t s = 0; s < fs->sectors_per_cluster && remaining > 0; s++) {
            if (read_sector(src_lba + s, sector) != FAT16_OK) {
                free_cluster_chain(fs, dst_first_cluster);
                return FAT16_ERR_IO;
            }
            if (write_sector(dst_lba + s, sector) != FAT16_OK) {
                free_cluster_chain(fs, dst_first_cluster);
                return FAT16_ERR_IO;
            }

            uint32_t moved = remaining > 512 ? 512 : remaining;
            remaining -= moved;
        }

        if (remaining == 0) {
            break;
        }

        src_cluster = read_fat_entry(fs, src_cluster);
        dst_cluster = read_fat_entry(fs, dst_cluster);
    }

    if (remaining != 0) {
        free_cluster_chain(fs, dst_first_cluster);
        return FAT16_ERR_IO;
    }

    dst_entry.first_cluster_low = dst_first_cluster;
    return write_dir_entry_at(dst_slot.lba, dst_slot.offset, &dst_entry);
}

int fat16_move_file(fat16_fs_t* fs, uint16_t src_dir_cluster, const char* src_name, uint16_t dst_dir_cluster, const char* dst_name) {
    uint8_t src83[11];
    uint8_t dst83[11];
    int rc = fat_name_to_83(src_name, src83);
    if (rc != FAT16_OK) {
        return rc;
    }
    rc = fat_name_to_83(dst_name, dst83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t src_entry;
    dir_slot_t src_slot;
    rc = find_entry_in_dir(fs, src_dir_cluster, src83, &src_entry, &src_slot);
    if (rc != FAT16_OK) {
        return rc;
    }
    if (src_entry.attr & FAT16_ATTR_DIRECTORY) {
        return FAT16_ERR_ISDIR;
    }

    if (src_dir_cluster == dst_dir_cluster && memcmp(src83, dst83, 11) == 0) {
        return FAT16_OK;
    }

    fat16_raw_dirent_t existing_dst;
    dir_slot_t existing_slot;
    rc = find_entry_in_dir(fs, dst_dir_cluster, dst83, &existing_dst, &existing_slot);
    if (rc == FAT16_OK) {
        if (existing_dst.attr & FAT16_ATTR_DIRECTORY) {
            return FAT16_ERR_ISDIR;
        }
        return FAT16_ERR_EXISTS;
    }
    if (rc != FAT16_ERR_NOT_FOUND) {
        return rc;
    }

    dir_slot_t dst_slot;
    rc = find_free_slot(fs, dst_dir_cluster, &dst_slot);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t moved_entry = src_entry;
    memcpy(moved_entry.name, dst83, 11);

    rc = write_dir_entry_at(dst_slot.lba, dst_slot.offset, &moved_entry);
    if (rc != FAT16_OK) {
        return rc;
    }

    return mark_entry_deleted(src_slot.lba, src_slot.offset);
}

int fat16_remove(fat16_fs_t* fs, uint16_t dir_cluster, const char* name) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strcmp(name, "/") == 0) {
        return FAT16_ERR_INVALID;
    }

    uint8_t name83[11];
    int rc = fat_name_to_83(name, name83);
    if (rc != FAT16_OK) {
        return rc;
    }

    fat16_raw_dirent_t entry;
    dir_slot_t slot;
    rc = find_entry_in_dir(fs, dir_cluster, name83, &entry, &slot);
    if (rc != FAT16_OK) {
        return rc;
    }

    if (entry.attr & FAT16_ATTR_DIRECTORY) {
        if (entry.first_cluster_low < 2) {
            return FAT16_ERR_INVALID;
        }
        if (!dir_is_empty(fs, entry.first_cluster_low)) {
            return FAT16_ERR_NOTEMPTY;
        }
        free_cluster_chain(fs, entry.first_cluster_low);
    } else if (entry.first_cluster_low >= 2) {
        free_cluster_chain(fs, entry.first_cluster_low);
    }

    return mark_entry_deleted(slot.lba, slot.offset);
}
