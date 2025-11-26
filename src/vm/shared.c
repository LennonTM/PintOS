#include <stdlib.h>
#include "vm/shared.h"
#include "filesys/off_t.h"
#include "lib/kernel/hash.h"

/* frame_table_entry is an array of all frame_table entries */
static struct hash shared_table;

/* Initialises shared table of read only executables */
void
shared_table_init (void) {
    /* Set up frame table */
    if (!hash_init(&shared_table, shared_hash, shared_less, NULL)) {
      PANIC("Failed to initialise shared_table");
    }
}

/* Returns a hash value for shared_entry p. */
unsigned
shared_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct shared_entry *p = hash_entry (p_, struct shared_entry, elem);
  unsigned hash_file = file_hash (p->file);
  unsigned hash_offset = hash_int ((int) p->offset);
  return hash_file ^ hash_offset;
}

/* Returns true if shared_entry a precedes shared_entry b. */
bool
shared_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED)
{
  const struct shared_entry *a = hash_entry (a_, struct shared_entry, elem);
  const struct shared_entry *b = hash_entry (b_, struct shared_entry, elem);
  if (file_compare (a->file, b->file)) {
    return a->offset < b->offset;
  }
  return file_less (a->file, b->file);
}