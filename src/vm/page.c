#include "vm/page.h"
#include "vm/shared.h"
#include "vm/frame.h"
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

/* Records data in SPT about a page to be lazy-loaded from a file */
void
spt_record_file_page (struct hash *spt, struct file *file, off_t ofs,
                      uint8_t *upage, uint32_t page_read_bytes,
                      uint32_t page_zero_bytes, bool writable)
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
  struct hash_elem* prev_elem = hash_insert (spt, &entry->elem);
  ASSERT (prev_elem == NULL);
}

/* Removes provided entry from the SPT
   returns true if entry was removed successfully */
bool 
spt_remove_entry (struct hash *spt, struct spt_entry *entry) {
  struct hash_elem *removed_elem = hash_delete (spt, &entry->elem);
  free (entry);
  return removed_elem != NULL;
}

/* Returns an address of an SPT entry
   corresponding to provided user vaddr of the page 
   NULL if not entry exists */
struct spt_entry *
spt_get_entry (struct hash *spt, void *upage) {
  struct spt_entry key_entry = (struct spt_entry) {
    .upage = upage
  };
  struct hash_elem *spt_entry_elem = hash_find (spt, &key_entry.elem);
  if (spt_entry_elem == NULL) {
    return NULL;
  }
  struct spt_entry *spt_entry =
    hash_entry (spt_entry_elem, struct spt_entry, elem);
  return spt_entry;
}

/* Helper function for destory_spt, destroys memory for the entry */
static void
spt_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spt_entry = hash_entry (e, struct spt_entry, elem);
  
  free (spt_entry); 
}

void
spt_destroy (struct hash *spt) {
  hash_destroy (spt, spt_destroy_entry);
}

bool
spt_load_file_page (struct spt_entry* spt_entry) {
  void *upage            = spt_entry->upage;
  bool writable          = spt_entry->writable;
  struct file *file      = spt_entry->aux.file.file;
  off_t offset           = spt_entry->aux.file.ofs;
  size_t page_read_bytes = spt_entry->aux.file.page_read_bytes;
  size_t page_zero_bytes = spt_entry->aux.file.page_zero_bytes;
  struct shared_entry *shared_entry;
  /* Check if the page has been loaded by other process */
  shared_entry = get_shared_entry (file, offset);
  if (shared_entry != NULL) {
    ASSERT (!writable);
    /* Link the user page to existing frame */
    uint8_t *kpage = shared_entry->kpage;
    spt_share_entry (spt_entry, &shared_entry->spt_ptrs);
    return frame_install_page (spt_entry->upage, kpage, writable);
  }
  
  /* Load a new page */
  uint8_t *kpage = load_page_from_file (file, offset, upage, page_read_bytes,
                                        page_zero_bytes, writable);
  if (kpage == NULL) {
    return false;
  }
  /* Store information about it in a shared table */
  if (!writable) {
    shared_entry = create_shared_entry (file, offset, kpage, page_read_bytes);
    spt_share_entry (spt_entry, &shared_entry->spt_ptrs);
  }
  else {
    /* TODO: Remove for now, for eviction probably need
     * to transition to another state, e.g., FILE_LOADED */
    spt_remove_entry (&thread_current()->process->spt, spt_entry);
  }

  return true;
}

/* Turn entry into shared one, and add it on a corresponding list */
void
spt_share_entry (struct spt_entry *spt_entry, struct list *shared_list) {
  spt_entry->status = SHARED;
  list_push_front (shared_list, &spt_entry->aux.shared.elem);
}

