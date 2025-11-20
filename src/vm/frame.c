#include <stddef.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"

/* Two pools: one for kernel data, one for user pages. */
static struct frame_table_entry *frame_table = NULL;

void
frame_table_init (void) {
    size_t user_pages = get_user_pages();
    frame_table = calloc(sizeof(struct frame_table_entry), user_pages);
    if (frame_table == NULL) 
        PANIC("Failed to malloc frame table!");
}