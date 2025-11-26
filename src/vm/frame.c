#include <stddef.h>
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

static struct lock frame_lock;
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

/* Selects a victim frame, evicts it to swap/file, and returns the kpage */
static void *
frame_evict (void) {
    size_t user_pages = get_user_pages();
    struct frame_table_entry *victim = NULL;
    void *kpage = NULL;

    while (true) {
        struct frame_table_entry *e = &frame_table[eviction_search_index];
        void *frame_addr = (void *)((uintptr_t)eviction_search_index * PGSIZE 
                            + (uintptr_t)palloc_get_user_pool_base());
        
        if (!e->pinned && e->owner != NULL) {
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

    struct spt_entry *spte = get_entry(&victim->owner->spt, victim->upage);
    ASSERT(spte != NULL);

    bool is_dirty = pagedir_is_dirty(victim->owner->pagedir, victim->upage);

    if (spte->status != FILE || is_dirty) {
        spte->status = SWAP;
        spte->aux.swap.index = swap_out(kpage);
    }

    pagedir_clear_page(victim->owner->pagedir, victim->upage);

    victim->owner = NULL;
    victim->upage = NULL;
    victim->pinned = false;

    return kpage;
}

/* Allocates a page using palloc_get_page
   the corresponding frame_table_entry will be used after */
void *
frame_alloc (enum palloc_flags flags) {
    if ((flags & PAL_USER) == 0) return palloc_get_page(flags);

    lock_acquire(&frame_lock);
    void *kpage = palloc_get_page (flags);

    if (kpage == NULL) {
        kpage = frame_evict();

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

    lock_release(&frame_lock);
    return kpage;   
}

/* Frees a page using palloc_free_page
   the corresponding frame_table_entry will be unused after */
void
frame_free (void *kpage) {
    palloc_free_page(kpage);
}

/* Sets owner and upage members of an allocated kpage */
void
frame_install_page(void *upage, void *kpage) {
    /* Assert that this frame has just been allocated */
    size_t frame_index = get_page_index(kpage);
    frame_table[frame_index].owner = thread_current()->process;
    frame_table[frame_index].upage = upage;
}