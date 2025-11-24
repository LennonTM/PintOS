#include "vm/page.h"
#include "lib/debug.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/malloc.h"

/* Returns a hash value for spt_entry p. */
unsigned
spt_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct spt_entry *p = hash_entry (p_, struct spt_entry, elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* Returns true if spt_entry a precedes spt_entry b. */
bool
spt_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED)
{
  const struct spt_entry *a = hash_entry (a_, struct spt_entry, elem);
  const struct spt_entry *b = hash_entry (b_, struct spt_entry, elem);
  return a->upage < b->upage;
}

void
record_file_page (struct file *file, off_t ofs, uint8_t *upage,
                  uint32_t page_read_bytes, uint32_t page_zero_bytes,
                  bool writable)
{
  struct spt_entry *entry = 
    (struct spt_entry *) malloc (sizeof (struct spt_entry));
  if (entry == NULL) {
    /* Kernel ran out of memory */
    process_exit (PROC_ERR);
  }
  entry->upage = upage;
  entry->writable = writable;
  entry->status = FILE;
  entry->aux.file.file = file;
  entry->aux.file.ofs = ofs;
  entry->aux.file.page_read_bytes = page_read_bytes;
  entry->aux.file.page_zero_bytes = page_zero_bytes;
  struct hash *spt = &thread_current()->process->spt;
  struct hash_elem* prev_elem = hash_insert (spt, &entry->elem);
  ASSERT (prev_elem == NULL);
}

