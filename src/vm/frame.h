#include <stddef.h>
#include <stdbool.h>
#include "threads/palloc.h"

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
void *frame_alloc (enum palloc_flags flags);
void frame_install_page(void *upage, void *kpage);
void frame_free (void *kpage);