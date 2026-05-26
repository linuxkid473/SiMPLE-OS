#ifndef USER_DIRENT_H
#define USER_DIRENT_H

typedef unsigned int  ino_t;
typedef unsigned int  off_t;
typedef unsigned char u8;

/* Matches the kernel's linux_dirent64 layout emitted by sys_getdents64 */
struct dirent64 {
    unsigned long long d_ino;
    long long          d_off;
    unsigned short     d_reclen;
    unsigned char      d_type;
    char               d_name[256];
};

/* Simplified dirent for our readdir wrapper */
struct dirent {
    ino_t  d_ino;
    char   d_name[256];
    unsigned char d_type;
};

#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4

/* Opaque DIR handle */
typedef struct _DIR DIR;

DIR           *opendir(const char *path);
struct dirent *readdir(DIR *d);
int            closedir(DIR *d);
void           rewinddir(DIR *d);

#endif /* USER_DIRENT_H */
