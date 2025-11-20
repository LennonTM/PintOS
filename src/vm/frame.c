#include <stddef.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "bitmap.h"

/* frame_table_entry is an array of all frame_table entries */
static struct frame_table_entry *frame_table = NULL;
/* A bitmap indicates whether each frame_table has yet to be allocated */
static struct bitmap *frame_bitmap = NULL;

void
frame_table_init (void) {
    size_t user_pages = get_user_pages();
    /* Set up frame table */
    frame_table = calloc(sizeof(struct frame_table_entry), user_pages);
    if (frame_table == NULL) 
        PANIC("Failed to malloc frame table!");

    /* Set up bitmap of user_page */
    frame_bitmap = bitmap_create (user_pages);
}