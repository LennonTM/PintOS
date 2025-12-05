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

#define PUSH_BYTES 4   /* PUSH can fault 4 bytes below esp */
#define PUSHA_BYTES 32 /* PUSHA can fault 32 bytes below esp */
#define STACK_GROWTH_MAX_SIZE 0x800000

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
  struct spt_entry *spte =
    (struct spt_entry *) malloc (sizeof (struct spt_entry));
  if (spte == NULL) {
    /* Kernel ran out of memory */
    process_exit (PROC_ERR);
  }
  spte->upage = upage;
  uint32_t *pd = thread_current()->process->pagedir;
  set_page_status (upage, status);
  pagedir_set_writable (pd, upage, writable);
  struct hash_elem *prev_elem = hash_insert (spt, &spte->elem);
  ASSERT (prev_elem == NULL);
  return spte;
}

/* Records a page in the SPT with the given status. */
void
spt_record_page (struct hash *spt, struct file *file, off_t ofs,
                 uint8_t *upage, uint32_t page_read_bytes,
                 bool writable, enum page_status status)
{
  struct spt_entry *spte = spt_create_entry (spt, upage, writable, status);
  spte->file = file;
  spte->ofs = ofs;
  spte->page_read_bytes = page_read_bytes;
}

/* Returns SPT entry for upage, or NULL if not found. */
struct spt_entry *
spt_get_entry (struct hash *spt, void *upage) {
  struct spt_entry spte;
  struct hash_elem *e;
  spte.upage = upage;
  e = hash_find (spt, &spte.elem);
  return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

/* Cleans up and frees an SPT entry during hash destruction. */
static void
spt_destroy_entry (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *spte = hash_entry (e, struct spt_entry, elem);
  uint32_t *pd = thread_current()->process->pagedir;
  void *upage = spte->upage;
  void *kpage = pagedir_get_page(pd, upage);
  bool is_dirty = pagedir_is_dirty (pd, upage);

  enum page_status status = get_page_status (upage);
  switch (status) {
    case SPT_INVALID:
      PANIC("HEY!");
      break;
    case SPT_FILE:
      if (is_dirty) {
        file_write_at (spte->file, kpage, spte->page_read_bytes, spte->ofs);
      }
      break;
    case SPT_SHARED:
      if (kpage != NULL) {
        unlink_shared_entry (spte->file, spte->ofs, spte);
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
  free (spte);
}

/* Destroys all entries in the SPT and frees associated resources. */
void
spt_destroy (struct hash *spt)
{
  hash_destroy (spt, spt_destroy_entry);
}

/* Removes and frees an SPT entry. Returns true on success. */
bool
spt_remove_entry (struct hash *spt, struct spt_entry *spte) {
  struct hash_elem *removed_elem = hash_delete (spt, &spte->elem);
  spt_destroy_entry (&spte->elem, NULL);
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
spt_load_file_page (struct spt_entry *spte)
{
  uint32_t *pd = thread_current()->process->pagedir;
  bool writable = pagedir_is_writable (pd, spte->upage);
  uint8_t *kpage = load_page_from_file (spte->upage,
                                        writable,
                                        spte->file, 
                                        spte->ofs,
                                        spte->page_read_bytes);
  return kpage;
}

/* Loads a shared read-only page, reusing existing frame if available. */
bool
spt_load_shared_page (struct spt_entry *spte)
{
  uint32_t *pd = thread_current()->process->pagedir;
  bool writable = pagedir_is_writable(pd, spte->upage);
  ASSERT(get_page_status(spte->upage) == SPT_SHARED);
  ASSERT (!writable);

  /* Link the spt_entry to the shared_entry */
  struct shared_entry *shared_entry =
    link_to_shared_entry (spte->file, spte->ofs, spte);
  if (shared_entry == NULL)
    return false;

  /* Atomically load the page */
  lock_acquire (&shared_entry->lock);
  uint8_t *kpage = shared_entry->kpage;
  if (kpage == NULL) {
    /* Load new page */
    kpage = spt_load_file_page(spte);
    if (kpage == NULL) {
      lock_release (&shared_entry->lock);
      unlink_shared_entry (spte->file, spte->ofs, spte);
      return false;
    }
    shared_entry->kpage = kpage;
  } else {
    /* Install an existing page */
    frame_install_page (spte->upage, kpage, writable);
  }
  lock_release (&shared_entry->lock);
  return true;
}

/* Removes the page at upage from the current process's SPT. */
void
spt_remove_page (void *upage)
{
  struct hash *spt = &thread_current()->process->spt;
  struct spt_entry *spte = spt_get_entry (spt, upage);
  spt_remove_entry (spt, spte);
}

enum page_status get_page_status (const void *upage) {
  uint32_t *pd = thread_current()->process->pagedir; 
  return pagedir_get_avl (pd, upage);
}

void set_page_status (const void *upage, enum page_status status) {
  uint32_t *pd = thread_current()->process->pagedir;
  pagedir_set_avl (pd, upage, status);
}

/* Claims a new page for the current process at fault_addr, 
   and passes in the stack pointer to check for a valid_stack_growth
   (the case in which no page table entry exists yet) */
bool spt_claim_page (void *fault_addr, void *esp) 
{
  struct process *proc = thread_current()->process; 
  void *fault_page = pg_round_down(fault_addr);

  /* Check via SPT */
  struct spt_entry *spt_entry = spt_get_entry (&proc->spt, fault_page);
  enum page_status status = get_page_status (fault_page);

  switch (status) {
    case SPT_INVALID:
      break;
    case SPT_SWAP:
      spt_load_swap_page (fault_page);
      set_page_status (fault_page, SPT_FRAME);
      return true;
    case SPT_FILE:
    case SPT_EXEC:
      /* Page is to be lazy-loaded from a file
          for both executable page and file page */
      spt_load_file_page (spt_entry);
      set_page_status (fault_page, status);
      return true;
    case SPT_SHARED:
      spt_load_shared_page (spt_entry);
      set_page_status (fault_page, SPT_SHARED);
      return true;
    case SPT_FRAME:
      PANIC ("FRAME page must always be present");
  }


  /* Check for stack growth:
     - fault_addr >= esp: normal stack access
     - fault_addr == esp - 4: PUSH instruction
     - fault_addr == esp - 32: PUSHA instruction */
  bool is_valid_stack_access = is_user_vaddr (fault_addr) &&
    (fault_addr >= esp ||
     fault_addr == esp - PUSH_BYTES ||
     fault_addr == esp - PUSHA_BYTES);

  if (is_valid_stack_access) 
  {
    /* Verify that the stack is less than STACK_GROWTH_MAX_SIZE */
    if (fault_addr < PHYS_BASE - STACK_GROWTH_MAX_SIZE)
      process_exit (PROC_ERR);
    /* Load the page */
    void *kpage = load_page_zeroing(fault_page, true);
    if (kpage == NULL) {
      process_exit (PROC_ERR);
    }
    /* Successfully grew the stack */
    set_page_status (fault_page, SPT_FRAME);
    return true;
  }

  return false;
}