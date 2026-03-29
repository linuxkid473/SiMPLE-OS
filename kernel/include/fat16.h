#ifndef SIMPLE_FAT16_H
#define SIMPLE_FAT16_H

#include "types.h"

#define FAT16_OK              0
#define FAT16_ERR_IO         -1
#define FAT16_ERR_NOT_FOUND  -2
#define FAT16_ERR_EXISTS     -3
#define FAT16_ERR_INVALID    -4
#define FAT16_ERR_NOSPACE    -5
#define FAT16_ERR_NOTDIR     -6
#define FAT16_ERR_ISDIR      -7
#define FAT16_ERR_NOTEMPTY   -8

#define FAT16_ATTR_DIRECTORY 0x10

typedef struct {
    uint32_t partition_lba;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t sectors_per_fat;
    uint16_t root_sectors;
    uint32_t total_sectors;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t data_lba;
    uint32_t max_clusters;
} fat16_fs_t;

typedef struct {
    char name[13];
    uint8_t attr;
    uint16_t first_cluster;
    uint32_t size;
} fat16_dirent_t;

int fat16_mount(fat16_fs_t* fs);
int fat16_list_dir(fat16_fs_t* fs, uint16_t dir_cluster);
int fat16_list_entries(fat16_fs_t* fs, uint16_t dir_cluster, fat16_dirent_t* entries, int max_entries, int* out_count);
int fat16_stat(fat16_fs_t* fs, uint16_t dir_cluster, const char* name, fat16_dirent_t* out_entry);
int fat16_change_dir(fat16_fs_t* fs, uint16_t dir_cluster, const char* name, uint16_t* out_cluster);
int fat16_mkdir(fat16_fs_t* fs, uint16_t dir_cluster, const char* name);
int fat16_touch(fat16_fs_t* fs, uint16_t dir_cluster, const char* name);
int fat16_read_file(fat16_fs_t* fs, uint16_t dir_cluster, const char* name, char* out, uint32_t max_len, uint32_t* out_len);
int fat16_write_file(fat16_fs_t* fs, uint16_t dir_cluster, const char* name, const char* data, uint32_t len);
int fat16_copy_file(fat16_fs_t* fs, uint16_t src_dir_cluster, const char* src_name, uint16_t dst_dir_cluster, const char* dst_name);
int fat16_move_file(fat16_fs_t* fs, uint16_t src_dir_cluster, const char* src_name, uint16_t dst_dir_cluster, const char* dst_name);
int fat16_remove(fat16_fs_t* fs, uint16_t dir_cluster, const char* name);

#endif
