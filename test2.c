typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
extern void kernel_main(void);
struct fb { int x; };
static struct fb fb_tag = {0};
struct s { uint32_t entry; uint32_t e2; uint32_t stack; uint32_t s2; uint64_t f; uint32_t tags; uint32_t t2; };
static struct s hdr = { .entry = (uint32_t)&kernel_main, .tags = (uint32_t)&fb_tag };
