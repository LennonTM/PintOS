#include <stddef.h>
#include <string.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "bitmap.h"
#include "userprog/process.h"
#include "threads/synch.h"

#include "userprog/pagedir.h"
#include "vm/page.h"
#include "devices/swap.h"

/* Lock prevents race conditions when accessing the frame table
for allocation and eviction */
struct lock frame_lock;

/* Used to iterate through frame table to find a 
victim frame to evict */
static size_t eviction_search_index = 0;

/* frame_table_entry is an array of all frame_table entries */
static struct frame_table_entry *frame_table = NULL;

/* Initialises frame_table_entry array */
void
frame_table_init (void) {
    size_t user_pages = get_user_pages();
    /* Set up frame table */
    frame_table = malloc(sizeof(struct frame_table_entry) * user_pages);
    if (frame_table == NULL) 
        PANIC("Failed to malloc frame table!");

    lock_init(&frame_lock);
}

/* Selects a frame to evict to swap disk, and returns the freed kpage address */
static void *
frame_evict (void) {
    size_t user_pages = get_user_pages();
    struct frame_table_entry *victim = NULL;
    void *kpage = NULL;

    /* We loop through all frames until a 'victim' is found */
    while (true) {
        struct frame_table_entry *e = &frame_table[eviction_search_index];
        void *frame_addr = (void *)((uintptr_t)eviction_search_index * PGSIZE 
                            + (uintptr_t)palloc_get_user_pool_base());
        
        if (!e->pinned && e->owner != NULL) {
            /* We check if the page has been used recently by checking the hardware
            'accessed' bit. If it is 1, we set it to 0. If it is 0, that means the
            page has not been accessed since our last pass. We choose this as our victim. */
            if (pagedir_is_accessed(e->owner->pagedir, e->upage)) {
                pagedir_set_accessed(e->owner->pagedir, e->upage, false);
            } else {
                victim = e;
                kpage = frame_addr;
                break;
            }
        }

        eviction_search_index = (eviction_search_index + 1) % user_pages;
    }

    ASSERT(victim != NULL);
    ASSERT(victim->owner != NULL);

    struct spt_entry *spte = spt_get_entry(&victim->owner->spt, victim->upage);
    ASSERT(spte != NULL);

    bool is_dirty = pagedir_is_dirty(victim->owner->pagedir, victim->upage);

    /* If page is dirty, we write it to swap to save the data */
    if (spte->status != FILE || is_dirty) {
        spte->status = SWAP;

        size_t slot_index = swap_out(kpage);
        memset(&spte->aux, 0, sizeof(spte->aux));

        spte->aux.swap.index = slot_index;
    }

    /* The next time a process touches the address, it will trigger a
    page fault, so the page can be loaded again */
    pagedir_clear_page(victim->owner->pagedir, victim->upage);

    /* Reset the frame table entry */
    victim->owner = NULL;
    victim->upage = NULL;
    victim->pinned = false;

    return kpage;
}

/* Allocates a page using palloc_get_page
   the corresponding frame_table_entry will be used after */
void *
frame_alloc (enum palloc_flags flags) {
    /* Kernel allocations bypass the frame table */
    if ((flags & PAL_USER) == 0) return palloc_get_page(flags);

    bool lock_held = lock_held_by_current_thread(&frame_lock);

    /* page_fault aquires the lock to safely read the SPT status.
    frame_alloc must check that the thread does not already hold
    the lock before trying to aquire it to prevent a deadlock.
    When the thread holds the lock, we can safely modify the
    global Frame Table. */
    if(!lock_held) lock_acquire(&frame_lock);

    /* Attempts to allocate a page from user pool */
    void *kpage = palloc_get_page (flags);

    /* If memory is full, we must evict a page to make room */
    if (kpage == NULL) {
        kpage = frame_evict();

        /* If a zeroed page is requested, we zero the evicted page */
        if (flags & PAL_ZERO) {
            memset(kpage, 0, PGSIZE);
        }
    }
    
    size_t frame_index = get_page_index(kpage);
    ASSERT(frame_index < get_user_pages());
    frame_table[frame_index] = (struct frame_table_entry) {
        .owner = NULL,
        .upage = NULL,
        .pinned = NULL,
        .accessed = NULL,
    };

    if (!lock_held) lock_release(&frame_lock);
    return kpage;   
}

/* Frees a page using palloc_free_page
   the corresponding frame_table_entry will be unused after */
void
frame_free (void *kpage) {
    palloc_free_page(kpage);
}

/* Maps user virtual address UPAGE to a frame at KPAGE
   by calling install_page
   Sets owner and upage members of a frame corresponding to kpage */
bool
frame_install_page(void *upage, void *kpage, bool writable) {
    if (!install_page (upage, kpage, writable)) {
      return false;
    }
    /* Assert that this frame has just been allocated */
    size_t frame_index = get_page_index(kpage);
    frame_table[frame_index].owner = thread_current()->process;
    frame_table[frame_index].upage = upage;
    return true;
}
