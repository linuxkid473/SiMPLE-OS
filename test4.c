#define _STIVALE2_SPLIT_64
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#define _stivale2_split64(NAME) \
    union {                    \
        uint32_t NAME;         \
        uint32_t NAME##_lo;    \
    };                         \
    uint32_t NAME##_hi

struct s { _stivale2_split64(entry); };
extern void kernel_main(void);
static struct s hdr = { .entry = (uint32_t)&kernel_main };
