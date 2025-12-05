#ifndef SHARED_H
#define SHARED_H

#include <stdlib.h>
#include "filesys/off_t.h"
#include "list.h"
#include "lib/kernel/hash.h"
#include <debug.h>
#include "filesys/file.h"
#include "vm/page.h"
#include "threads/synch.h"

struct shared_entry {
  struct file *file;        /* File to read from (part of key). */
  off_t offset;             /* Offset in file (part of key). */
  size_t page_read_bytes;   /* Bytes to read from file. */
  struct lock lock;         /* Protects kpage and reference_count. */
  void *kpage;              /* Kernel address of shared frame, or NULL. */
  int reference_count;      /* Number of processes sharing this entry. */
  struct hash_elem elem;
};

unsigned shared_hash (const struct hash_elem *p_, void *aux UNUSED);
bool shared_less (const struct hash_elem *a_, const struct hash_elem *b_,
                  void *aux UNUSED);

void shared_table_init (void);
struct shared_entry *get_shared_entry (struct file *file, off_t offset);
struct shared_entry *link_to_shared_entry (struct file *file, off_t offset,
                                           struct spt_entry *spt_entry);
void unlink_shared_entry (struct file *file, off_t offset,
                          struct spt_entry *spt_entry, uint32_t *pd,
                          bool is_eviction);
#endif
