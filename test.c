typedef unsigned int uint32_t;
extern void kernel_main(void);
struct s { uint32_t entry; };
struct s hdr = { .entry = (uint32_t)&kernel_main };
