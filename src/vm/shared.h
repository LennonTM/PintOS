#ifndef SHARED_H
#define SHARED_H

#include <stdlib.h>
#include "filesys/off_t.h"
#include "list.h"
#include "lib/kernel/hash.h"
#include <debug.h>
#include "filesys/file.h"

/* Entry to the read-only-executable page table. */
struct shared_entry {
  struct file *file;      /* part of key */
  off_t offset;           /* part of key */

  void *kpage; /* Stores the relevant or NULL if not loaded/ evicted */

  struct list spt_ptrs;

  struct hash_elem elem;
};

struct shared_list_elem {
  struct spt_entry *spt_ptr;
  struct list_elem elem;
};

/* Returns a hash value for shared_entry p. */
unsigned
shared_hash (const struct hash_elem *p_, void *aux UNUSED);

/* Returns true if spt_entry a precedes spt_entry b. */
bool
shared_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED);

/* Initialises shared table of read only executables */
void shared_table_init (void);

#endif