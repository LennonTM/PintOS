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
    frame_table = calloc(sizeof(struct frame_table_entry), user_pages);
    if (frame_table == NULL) 
        PANIC("Failed to malloc frame table!");
}

/* Allocates a page using palloc_get_page
   the corresponding frame_table_entry will be available also */
void *
frame_alloc (void) {
    /* allocate bitmap */
    void *kpage = palloc_get_page (PAL_USER); 
    if (kpage == NULL) {
        /* TODO: implement virtual memory */
        PANIC("Out of frames!!!");
    }

    return kpage;   
}

/* Sets owner and upage members of an allocated kpage */
void
frame_set_upage(void *kpage, void *upage) {
    /* Assert that this frame has just been allocated */
    size_t frame_index = vtop(kpage) / PGSIZE;
    frame_table[frame_index].owner = thread_current()->process;
    frame_table[frame_index].upage = upage;
}