#include <stddef.h>
#include <stdbool.h>

struct frame_table_entry {
    /* frame_table index tells us the kernel address and physical address */
    
    /* Pointer to owning process for eviction handling */
    struct process *owner;
    /* User virtual address for eviction handling */
    void *upage;

    /* Ensure kernel doesn't page fault accessing */
    bool pinned;

    /* Accessed bit for eviction policy */
    bool accessed;
};

void frame_table_init (void);
void *frame_alloc (void);
void frame_set_upage(void *kpage, void *upage);