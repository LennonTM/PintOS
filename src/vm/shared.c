#include <stdlib.h>
#include "vm/shared.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/off_t.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"

/* frame_table_entry is an array of all frame_table entries */
static struct hash shared_table;

/* Initialises shared table of read only executables */
void
shared_table_init (void) {
  if (!hash_init(&shared_table, shared_hash, shared_less, NULL)) {
    PANIC("Failed to initialise shared_table");
  }
}

/* Retrieve entry from the table by file pointer and offset */
struct shared_entry *
get_shared_entry (struct file *file, off_t offset) {
  struct shared_entry key_entry = (struct shared_entry) {
    .file = file,
    .offset = offset
  };
  struct hash_elem *shared_entry_elem =
    hash_find (&shared_table, &key_entry.elem);
  if (shared_entry_elem == NULL) {
    return NULL;
  }
  struct shared_entry *shared_entry =
    hash_entry (shared_entry_elem, struct shared_entry, elem);
  return shared_entry;
}

/* Creates initial shared_entry for the first process to load the page */
struct shared_entry *
create_shared_entry (struct file *file, off_t offset,
                     void *kpage, size_t page_read_bytes)
{
  struct shared_entry *shared_entry =
    malloc (sizeof (struct shared_entry));
  if (shared_entry == NULL) {
    return NULL;
  }
  shared_entry->file = file;
  shared_entry->offset = offset;
  shared_entry->kpage = kpage;
  shared_entry->page_read_bytes = page_read_bytes;
  list_init (&shared_entry->spt_ptrs);

  struct hash_elem *prev_elem =
    hash_insert (&shared_table, &shared_entry->elem);
  ASSERT (prev_elem == NULL);
  
  return shared_entry;
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
