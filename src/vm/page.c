#include "vm/page.h"
#include "vm/shared.h"
#include "vm/frame.h"
#include "threads/vaddr.h"
#include "lib/debug.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "devices/swap.h"

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

/* Records the file at page starting at upage in spt. ofs is the offset within
   the file. page_read_bytes is the number of bytes that can be read of the
   file in the page, page_zero_bytes is the rest of the bytes in the page 
   which are zero.*/
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
  struct hash_elem* prev_elem = hash_insert (spt, &entry->elem);
  ASSERT (prev_elem == NULL);
}

/* Record executable page in SPT
   if writable -> SPT_SHARED (read-only shared exec page)
   otherwise   -> SPT_EXEC   (writable exec page to be lazy-loaded) */
void
spt_record_exec_page (struct hash *spt, struct file *file, off_t ofs,
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
  entry->status = writable ? SPT_EXEC : SPT_SHARED;
  entry->aux.file.file = file;
  entry->aux.file.ofs = ofs;
  entry->aux.file.page_read_bytes = page_read_bytes;
  struct hash_elem* prev_elem = hash_insert (spt, &entry->elem);
  ASSERT (prev_elem == NULL);
}

void
spt_record_swap_page (struct hash *spt, uint8_t *upage, bool writable,
                      size_t swap_index) {
  struct spt_entry *entry =
    (struct spt_entry *) malloc (sizeof (struct spt_entry));
  if (entry == NULL) {
    /* Kernel ran out of memory */
    process_exit (PROC_ERR);
  }
  entry->upage = upage;
  entry->writable = writable;
  entry->status = SWAP;
  entry->aux.swap.index = swap_index;
  struct hash_elem* prev_elem = hash_insert (spt, &entry->elem);
  ASSERT (prev_elem == NULL);
}

void
spt_record_frame_page (struct hash *spt, uint8_t *upage, bool writable,
                       void *kpage) {
  struct spt_entry *entry =
    (struct spt_entry *) malloc (sizeof (struct spt_entry));
  if (entry == NULL) {
    /* Kernel ran out of memory */
    process_exit (PROC_ERR);
  }
  entry->upage = upage;
  entry->writable = writable;
  entry->status = FRAME;
  struct hash_elem* prev_elem = hash_insert (spt, &entry->elem);
  ASSERT (prev_elem == NULL);
}

/* Returns an address of an SPT entry
   corresponding to provided user vaddr of the page 
   NULL if not entry exists */
struct spt_entry *
spt_get_entry (struct hash *spt, void *upage) {
  struct spt_entry p;
  struct hash_elem *e;
  p.upage = upage;
  e = hash_find (spt, &p.elem);
  return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

/* Helper function for destory_spt, destroys memory for the entry */
static void
spt_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spt_entry = hash_entry (e, struct spt_entry, elem);
  void *kpage = pagedir_get_page(thread_current()->process->pagedir,
                                 spt_entry->upage);
  bool is_dirty = pagedir_is_dirty (thread_current()->process->pagedir,
                                    spt_entry->upage);

  switch (spt_entry->status) {
    case FILE:
      if (is_dirty) {
        struct file_aux *f = &spt_entry->aux.file;
        file_write_at (f->file, kpage, f->page_read_bytes, f->ofs);
      }
      break;
    case SPT_SHARED:
      if (kpage != NULL) {
        unlink_shared_entry (spt_entry->aux.file.file,
                              spt_entry->aux.file.ofs,
                              spt_entry);
      }
      break;
    case SWAP:
      ASSERT (kpage == NULL);
      /* Only stack pages are stored in the swap space,
         so reclaim swap space by dropping the page */
      swap_drop (spt_entry->aux.swap.index);
      break;
    case SPT_EXEC:
      break;
    case FRAME:
      ASSERT (kpage != NULL);
      break;
  }
  if (kpage != NULL) {
    frame_free (kpage);
  }
  free (spt_entry); 
}

void
spt_destroy (struct hash *spt) {
  hash_destroy (spt, spt_destroy_entry);
}

/* Removes provided entry from the SPT
   returns true if entry was removed successfully */
bool 
spt_remove_entry (struct hash *spt, struct spt_entry *entry) {
  struct hash_elem *removed_elem = hash_delete (spt, &entry->elem);
  spt_destroy_entry (&entry->elem, NULL);
  return removed_elem != NULL;
}

/* Load a writable page from a file
   (all read-only pages are shared) */
bool
spt_load_file_page (struct spt_entry* spt_entry) {
  void *upage            = spt_entry->upage;
  bool writable          = spt_entry->writable;
  struct file *file      = spt_entry->aux.file.file;
  off_t offset           = spt_entry->aux.file.ofs;
  size_t page_read_bytes = spt_entry->aux.file.page_read_bytes;
  size_t page_zero_bytes = PGSIZE - page_read_bytes;

  ASSERT (writable);

  uint8_t *kpage = load_page_from_file (file, offset, upage, page_read_bytes,
                                        page_zero_bytes, writable);
  if (kpage == NULL) {
    return false;
  }
  return true;
}

/* Load read-only shared page into memory
   allocate new kpage if no other process managed
   to load the page for this frame,
   otherwise, link to the existing page */
bool
spt_load_shared_page (struct spt_entry* spt_entry) {
  void *upage            = spt_entry->upage;
  bool writable          = spt_entry->writable;
  struct file *file      = spt_entry->aux.file.file;
  off_t offset           = spt_entry->aux.file.ofs;
  size_t page_read_bytes = spt_entry->aux.file.page_read_bytes;
  size_t page_zero_bytes = PGSIZE - page_read_bytes;

  ASSERT (!writable);
  /* Link the spt_entry to the shared_entry */
  struct shared_entry *shared_entry =
    link_to_shared_entry (file, offset, spt_entry);
  if (shared_entry == NULL) {
    return false;
  }
  /* Atomically load the page */
  lock_acquire (&shared_entry->lock);
  uint8_t *kpage = shared_entry->kpage;
  /* Load new page */
  if (kpage == NULL) {
    kpage = load_page_from_file (file, offset, upage, page_read_bytes,
                                 page_zero_bytes, writable);
    /* If load failed */
    if (kpage == NULL) {
      lock_release (&shared_entry->lock);
      unlink_shared_entry (file, offset, spt_entry);
      return false;
    }
    shared_entry->kpage = kpage;
  }
  else {
    /* Install an existing page */
    frame_install_page (spt_entry->upage, kpage, writable);
  }
  lock_release (&shared_entry->lock);

  return true;
}

/* Removes the page at address upage in the spt table. */
void
spt_remove_page (void* upage) {
  struct hash *spt = &thread_current()->process->spt;
  struct spt_entry *spt_entry = spt_get_entry (spt, upage);
  spt_remove_entry (spt, spt_entry);
}

