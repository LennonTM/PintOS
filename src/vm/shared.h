#ifndef SHARED_H
#define SHARED_H

#include <stdlib.h>
#include "filesys/off_t.h"
#include "list.h"
#include "lib/kernel/hash.h"
#include <debug.h>
#include "filesys/file.h"
#include "vm/page.h"

/* Entry to the read-only-executable page table. */
struct shared_entry {
  struct file *file;        /* File to read from (part of key) */
  off_t offset;             /* Offset in the file (part of key) */
  size_t page_read_bytes;   /* Number of bytes to read from the file. */

  void *kpage;              /* Kernel virtual address of shared frame */
  struct list spt_ptrs;     /* SPT entries for pages sharing this frame */

  struct hash_elem elem;
};

unsigned
shared_hash (const struct hash_elem *p_, void *aux UNUSED);

bool
shared_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED);

void shared_table_init (void);

struct shared_entry *
create_shared_entry (struct file *file, off_t offset,
                     void *kpage, size_t page_read_bytes);

struct shared_entry *
get_shared_entry (struct file *file, off_t offset);

#endif
