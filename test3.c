typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
extern void kernel_main(void);
struct s { 
    union { uint32_t entry; uint32_t entry_lo; }; 
    uint32_t entry_hi; 
};
static struct s hdr = { .entry = (uint64_t)(uint32_t)&kernel_main };
