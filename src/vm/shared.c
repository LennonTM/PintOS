#include <stdlib.h>
#include "vm/shared.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/off_t.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"

/* frame_table_entry is an array of all frame_table entries */
static struct hash shared_table;
static struct lock shared_table_lock;

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

/* Initialises shared table of read only executables */
void
shared_table_init (void) {
  if (!hash_init(&shared_table, shared_hash, shared_less, NULL)) {
    PANIC("Failed to initialise shared_table");
  }
  lock_init (&shared_table_lock);
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

/* Creates empty initial shared_entry
   Shared table lock MUST be held */
static struct shared_entry *
create_shared_entry (struct file *file, off_t offset)
{
  struct shared_entry *shared_entry =
    malloc (sizeof (struct shared_entry));
  if (shared_entry == NULL) {
    return NULL;
  }
  shared_entry->file = file;
  shared_entry->offset = offset;
  shared_entry->kpage = NULL;
  list_init (&shared_entry->spt_ptrs);
  lock_init (&shared_entry->lock);

  struct hash_elem *prev_elem =
    hash_insert (&shared_table, &shared_entry->elem);
  ASSERT (prev_elem == NULL);
  
  return shared_entry;
}

/* Links provided spt_entry to shared entry for {file, offset} */
struct shared_entry *
link_to_shared_entry (struct file *file, off_t offset,
                      struct spt_entry *spt_entry)
{
  ASSERT (!spt_entry->writable);
  ASSERT (spt_entry->status == FILE);

  /* Atomically get shared entry and create it if it doesn't exist */
  lock_acquire (&shared_table_lock);
  struct shared_entry *shared_entry = get_shared_entry (file, offset);
  if (shared_entry == NULL) {
    shared_entry = create_shared_entry (file, offset);
    if (shared_entry == NULL) {
      lock_release (&shared_table_lock);
      return NULL;
    }
  }
  /* It is important to add spt_entry to a list of
     spt pointers while HOLDING SHARED TABLE LOCK,
     so that no other process destroys the entry */
  list_push_front (&shared_entry->spt_ptrs, &spt_entry->aux.file.elem);
  lock_release (&shared_table_lock);
  return shared_entry;
}

void
unlink_shared_entry (struct file *file, off_t offset,
                     struct spt_entry *spt_entry)
{
  ASSERT (spt_entry->status == FILE && !spt_entry->writable);
  /* Unlink while holding a shared table lock to ensure that
     no other process links to the entry for given {file, offset}
     This allows to  */
  lock_acquire (&shared_table_lock);
  struct shared_entry *shared_entry = get_shared_entry (file, offset);
  /* If shared_entry is NULL, it has already been freed by another process */
  if (shared_entry == NULL) {
    lock_release(&shared_table_lock);
    return;
  }
  list_remove (&spt_entry->aux.file.elem);
  if (list_empty(&shared_entry->spt_ptrs)) {
    /* Destroy the entry if the process was the last one sharing it */
    struct hash_elem *removed_elem = 
      hash_delete (&shared_table, &shared_entry->elem);
    ASSERT (removed_elem != NULL);
    free (shared_entry);
  }
  else {
    /* If other processes still share the frame
       Uninstall the page pointing to it */
    uninstall_page (spt_entry->upage);
  }
  lock_release (&shared_table_lock);
}

