#ifndef FRAME_H
#define FRAME_H

#include <stddef.h>
#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/synch.h"

struct frame_table_entry {
  /* List of processes and user page addresses
     that map to this frame */
  struct list owners;
  /* Ensure kernel doesn't page fault accessing */
  bool pinned;
};

struct frame_owner {
  /* User virtual address that maps to the frame */
  void *upage;
  /* Process which holds the pagedir for upage -> frame mapping */
  struct process *process;
  /* Element on frame_table_entry list */
  struct list_elem elem;
};

void frame_table_init (void);
void *frame_alloc (enum palloc_flags flags);
bool frame_install_page (void *upage, void *kpage, bool writable);
void frame_free (void *kpage);
extern struct lock frame_lock;
#endif
