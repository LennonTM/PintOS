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

#include <stdio.h>

/* Hash function for SPT entries based on upage. */
unsigned
spt_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct spt_entry *p = hash_entry (p_, struct spt_entry, elem);
  return hash_bytes (&p->upage, sizeof p->upage);
}

/* Comparison function for SPT entries. */
bool
spt_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED)
{
  const struct spt_entry *a = hash_entry (a_, struct spt_entry, elem);
  const struct spt_entry *b = hash_entry (b_, struct spt_entry, elem);
  return a->upage < b->upage;                           
}

/* Allocates and inserts a new SPT entry. Panics on allocation failure. */
static struct spt_entry *
spt_create_entry (struct hash *spt, uint8_t *upage, bool writable,
                  enum page_status status)
{
  struct spt_entry *entry =
    (struct spt_entry *) malloc (sizeof (struct spt_entry));
  if (entry == NULL) {
    /* Kernel ran out of memory */
    process_exit (PROC_ERR);
  }
  entry->upage = upage;
  uint32_t *pd = thread_current()->process->pagedir;
  set_page_status (upage, status);
  pagedir_set_writable (pd, upage, writable);
  struct hash_elem *prev_elem = hash_insert (spt, &entry->elem);
  ASSERT (prev_elem == NULL);
  return entry;
}

/* Records a file-backed page in the SPT. */
void
spt_record_file_page (struct hash *spt, struct file *file, off_t ofs,
                      uint8_t *upage, uint32_t page_read_bytes,
                      bool writable)
{
  struct spt_entry *entry = spt_create_entry (spt, upage, writable, SPT_FILE);
  entry->aux.file.file = file;
  entry->aux.file.ofs = ofs;
  entry->aux.file.page_read_bytes = page_read_bytes;
}

/* Records an executable page: SPT_EXEC if writable, SPT_SHARED otherwise. */
void
spt_record_exec_page (struct hash *spt, struct file *file, off_t ofs,
                      uint8_t *upage, uint32_t page_read_bytes,
                      bool writable)
{
  enum page_status status = writable ? SPT_EXEC : SPT_SHARED;
  struct spt_entry *entry = spt_create_entry (spt, upage, writable, status);
  entry->aux.file.file = file;
  entry->aux.file.ofs = ofs;
  entry->aux.file.page_read_bytes = page_read_bytes;
}

/* Returns SPT entry for upage, or NULL if not found. */
struct spt_entry *
spt_get_entry (struct hash *spt, void *upage) {
  struct spt_entry p;
  struct hash_elem *e;
  p.upage = upage;
  e = hash_find (spt, &p.elem);
  return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

/* Cleans up and frees an SPT entry during hash destruction. */
static void
spt_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spt_entry = hash_entry (e, struct spt_entry, elem);
  uint32_t *pd = thread_current()->process->pagedir;
  void *upage = spt_entry->upage;
  void *kpage = pagedir_get_page(pd, upage);
  bool is_dirty = pagedir_is_dirty (pd, upage);

  enum page_status status = get_page_status (upage);
  switch (status) {
    case SPT_INVALID:
      PANIC("HEY!");
      break;
    case SPT_FILE:
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
    case SPT_SWAP:
      PANIC("SPT_SWAP has no SPT entry");
      break;
    case SPT_EXEC:
      break;
    case SPT_FRAME:
      PANIC("SPT_FRAME has no SPT entry");
      break;
  }
  if (kpage != NULL) {
    pagedir_clear_page (pd, upage);
    frame_free (kpage);
  }
  /* Mark page as invalid so future faults don't try to load it */
  set_page_status (upage, SPT_INVALID);
  free (spt_entry); 
}

/* Destroys all entries in the SPT and frees associated resources. */
void
spt_destroy (struct hash *spt)
{
  hash_destroy (spt, spt_destroy_entry);
}

/* Removes and frees an SPT entry. Returns true on success. */
bool
spt_remove_entry (struct hash *spt, struct spt_entry *entry) {
  struct hash_elem *removed_elem = hash_delete (spt, &entry->elem);
  spt_destroy_entry (&entry->elem, NULL);
  return removed_elem != NULL;
}

/* Loads a writable page from file into memory. */
bool
spt_load_swap_page (void *upage)
{
  uint32_t *pd = thread_current()->process->pagedir;
  bool writable = pagedir_is_writable (pd, upage);
  size_t swap_slot = pagedir_get_swap (pd, upage);
  bool success = load_page_from_swap (upage,
                                      writable,
                                      swap_slot);
  return success;
}

/* Loads a writable page from file into memory. */
uint8_t *
spt_load_file_page (struct spt_entry *spt_entry)
{
  struct file_aux *f = &spt_entry->aux.file;
  uint32_t *pd = thread_current()->process->pagedir;
  bool writable = pagedir_is_writable (pd, spt_entry->upage);
  uint8_t *kpage = load_page_from_file (spt_entry->upage,
                                        writable,
                                        f->file, 
                                        f->ofs,
                                        f->page_read_bytes);
  return kpage;
}

/* Loads a shared read-only page, reusing existing frame if available. */
bool
spt_load_shared_page (struct spt_entry *spt_entry)
{
  uint32_t *pd = thread_current()->process->pagedir;
  bool writable = pagedir_is_writable(pd, spt_entry->upage);
  ASSERT(get_page_status(spt_entry->upage) == SPT_SHARED);
  ASSERT (!writable);
  struct file_aux *f = &spt_entry->aux.file;

  /* Link the spt_entry to the shared_entry */
  struct shared_entry *shared_entry =
    link_to_shared_entry (f->file, f->ofs, spt_entry);
  if (shared_entry == NULL)
    return false;

  /* Atomically load the page */
  lock_acquire (&shared_entry->lock);
  uint8_t *kpage = shared_entry->kpage;
  if (kpage == NULL) {
    /* Load new page */
    kpage = spt_load_file_page(spt_entry);
    if (kpage == NULL) {
      lock_release (&shared_entry->lock);
      unlink_shared_entry (f->file, f->ofs, spt_entry);
      return false;
    }
    shared_entry->kpage = kpage;
  } else {
    /* Install an existing page */
    frame_install_page (spt_entry->upage, kpage, writable);
  }
  lock_release (&shared_entry->lock);
  return true;
}

/* Removes the page at upage from the current process's SPT. */
void
spt_remove_page (void *upage)
{
  struct hash *spt = &thread_current()->process->spt;
  struct spt_entry *spt_entry = spt_get_entry (spt, upage);
  spt_remove_entry (spt, spt_entry);
}

enum page_status get_page_status (const void *upage) {
  uint32_t *pd = thread_current()->process->pagedir; 
  return pagedir_get_avl (pd, upage);
}

void set_page_status (const void *upage, enum page_status status) {
  uint32_t *pd = thread_current()->process->pagedir;
  pagedir_set_avl (pd, upage, status);
}

