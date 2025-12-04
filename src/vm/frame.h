#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/synch.h"

struct frame_table_entry {
  struct list owners;  /* Processes mapping to this frame. */
  bool pinned;         /* If true, frame cannot be evicted. */
};

struct frame_owner {
  void *upage;            /* User virtual address mapping to frame. */
  struct process *process; /* Owning process. */
  struct list_elem elem;
};

void frame_table_init (void);
void *frame_alloc (enum palloc_flags flags);
bool frame_install_page (void *upage, void *kpage, bool writable);
void frame_free (void *kpage);
#endif
