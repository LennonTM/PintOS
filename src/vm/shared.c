#include <stdlib.h>
#include "vm/shared.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "filesys/off_t.h"
#include "lib/kernel/hash.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"

/* Global table of shared read-only executable pages. */
static struct hash shared_table;
static struct lock shared_table_lock;

/* Hash function for shared entries based on file and offset. */
unsigned
shared_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct shared_entry *p = hash_entry (p_, struct shared_entry, elem);
  unsigned hash_file = file_hash (p->file);
  unsigned hash_offset = hash_int ((int) p->offset);
  return hash_file ^ hash_offset;
}

/* Comparison function for shared entries. */
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

/* Initializes the shared page table. */
void
shared_table_init (void)
{
  if (!hash_init(&shared_table, shared_hash, shared_less, NULL)) {
    PANIC("Failed to initialise shared_table");
  }
  lock_init (&shared_table_lock);
}

/* Returns shared entry for {file, offset}, or NULL if not found. */
struct shared_entry *
get_shared_entry (struct file *file, off_t offset)
{
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

/* Creates a new shared entry. Caller must hold shared_table_lock. */
static struct shared_entry *
create_shared_entry (struct file *file, off_t offset)
{
  struct shared_entry *shared_entry =
    malloc (sizeof (struct shared_entry));
  if (shared_entry == NULL) {
    return NULL;
  }
  shared_entry->file = file_reopen (file);
  shared_entry->offset = offset;
  shared_entry->kpage = NULL;
  shared_entry->reference_count = 0;
  lock_init (&shared_entry->lock);

  struct hash_elem *prev_elem =
    hash_insert (&shared_table, &shared_entry->elem);
  ASSERT (prev_elem == NULL);
  
  return shared_entry;
}

/* Links spt_entry to shared entry, creating one if needed. */
struct shared_entry *
link_to_shared_entry (struct file *file, off_t offset,
                      struct spt_entry *spt_entry)
{
  uint32_t *pd = thread_current()->process->pagedir;
  bool writable = pagedir_is_writable (pd, spt_entry->upage);
  ASSERT (!writable);
  ASSERT (get_page_status (spt_entry->upage) == SPT_SHARED);

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
  /* It is important to increment/decrement refference count while holding lock
     so that only a single process destroys the entry (when the count = 0).*/
  shared_entry->reference_count++;
  lock_release (&shared_table_lock);
  return shared_entry;
}

/* Unlinks spt_entry from shared entry, destroying entry if last reference. */
void
unlink_shared_entry (struct file *file, off_t offset,
                     struct spt_entry *spt_entry, uint32_t *pd)
{
  /* Use pd parameter, not thread_current()->process->pagedir,
     since this may be called during eviction from another process's context */
  ASSERT (pagedir_get_avl (pd, spt_entry->upage) == SPT_SHARED);
  bool writable = pagedir_is_writable (pd, spt_entry->upage);
  ASSERT (!writable);
  /* Unlink while holding a shared table lock to ensure that
     no other process links to the entry for given {file, offset}
     This allows to  */
  lock_acquire (&shared_table_lock);
  struct shared_entry *shared_entry = get_shared_entry (file, offset);
  ASSERT (shared_entry != NULL);
  shared_entry->reference_count--;
  if (shared_entry->reference_count == 0) {
    /* Destroy the entry if the process was the last one sharing it */
    struct hash_elem *removed_elem = 
      hash_delete (&shared_table, &shared_entry->elem);
    ASSERT (removed_elem != NULL);
    file_close (shared_entry->file);
    free (shared_entry);
  }
  else {
    /* If other processes still share the frame
       Uninstall the page pointing to it.
       Use pd parameter since this may be called during eviction
       from another process's context */
    pagedir_clear_page (pd, spt_entry->upage);
  }
  lock_release (&shared_table_lock);
}

