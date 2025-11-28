#include <stddef.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "bitmap.h"
#include "userprog/process.h"

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
}

/* Allocates a page using palloc_get_page
   the corresponding frame_table_entry will be used after */
void *
frame_alloc (enum palloc_flags flags) {
    void *kpage = palloc_get_page (flags); 
    if (kpage == NULL) {
        /* TODO: implement virtual memory */
        PANIC("Out of frames!!!");
    }
    
    size_t frame_index = get_page_index(kpage);
    ASSERT(frame_index < get_user_pages());
    frame_table[frame_index] = (struct frame_table_entry) {
        .owner = NULL,
        .upage = NULL,
        .pinned = NULL,
        .accessed = NULL,
    };

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
