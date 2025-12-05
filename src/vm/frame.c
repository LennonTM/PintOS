#include <stddef.h>
#include <string.h>
#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "bitmap.h"
#include "userprog/process.h"
#include "threads/synch.h"

#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/shared.h"
#include "devices/swap.h"

/* Protects frame table during allocation and eviction. */
static struct lock frame_lock;

/* Clock hand for second-chance eviction algorithm. */
static size_t eviction_search_index = 0;

/* Array of frame table entries, one per user page. */
static struct frame_table_entry *frame_table = NULL;

/* Initializes the frame table. */
void
frame_table_init (void)
{
  size_t user_pages = get_user_pages();
  /* Set up frame table */
  frame_table = malloc(sizeof(struct frame_table_entry) * user_pages);
  if (frame_table == NULL) 
    PANIC("Failed to malloc frame table!");

  lock_init(&frame_lock);
}

/* Evicts a frame using second-chance algorithm. Returns freed kpage. */
static void *
frame_evict (void)
{
  size_t user_pages = get_user_pages();
  struct frame_table_entry *victim = NULL;
  void *kpage = NULL;

  /* We loop through all frames until a 'victim' is found */
  while (true) {
    struct frame_table_entry *frame = &frame_table[eviction_search_index];
    void *frame_addr = (void *)((uintptr_t)eviction_search_index * PGSIZE 
                        + (uintptr_t)palloc_get_user_pool_base());
    eviction_search_index = (eviction_search_index + 1) % user_pages;
    if (frame->pinned) {
      continue;
    }
    bool accessed = false;
    ASSERT (!list_empty (&frame->owners));
    for (struct list_elem *e = list_begin (&frame->owners);
         e != list_end (&frame->owners);
         e = list_next (e))
    {
      struct frame_owner *owner = list_entry (e, struct frame_owner, elem);
      /* Check accessed bit of the pte corresponding to owner->upage
         which maps to current frame. Unset accessed bit of pte
         and choose the frame as a victim if all pages referring to it
         have not been accessed (or bits were unset by previous pass) */
      if (pagedir_is_accessed(owner->process->pagedir, owner->upage)) {
        pagedir_set_accessed(owner->process->pagedir, owner->upage, false);
        accessed = true;
      }
    }

    if (!accessed) {
      victim = frame;
      kpage = frame_addr;
      break;
    }
  }

  ASSERT(victim != NULL);

  struct list_elem *e = list_begin (&victim->owners);
  while (e != list_end (&victim->owners))
  {
    struct frame_owner *owner = list_entry (e, struct frame_owner, elem);
    struct spt_entry *spte =
      spt_get_entry(&owner->process->spt, owner->upage);
    uint32_t *pd = owner->process->pagedir;

    /* The next time a process touches the address, it will trigger a
    page fault, so the page can be loaded again */
    pagedir_clear_page(pd, owner->upage);
    
    bool is_dirty = pagedir_is_dirty(pd, owner->upage);
    bool writable = pagedir_is_writable(pd, owner->upage);

    /* All frame owner(s) hold necessary information in the spt_entry */
    enum page_status status = pagedir_get_avl (pd, owner->upage);
    switch (status) {
      case SPT_FILE:
        ASSERT (writable);
        /* If a file page is dirty, write it to the file */
        if (is_dirty) {
          file_write_at (spte->file, kpage, spte->page_read_bytes, spte->ofs);
        }
        /* Otherwise the page is cleared from memory, but
            spt entry is kept to load it again later */
        break;
      case SPT_SHARED:
        ASSERT (!writable);
        unlink_shared_entry (spte->file, spte->ofs, spte);
        break;
      case SPT_EXEC:
        /* Dirty exec pages: remove SPT entry 
           (no longer file-backed) and swap out */
        if (is_dirty) {
          spt_remove_entry (&owner->process->spt, spte);
          size_t swap_index = swap_out(kpage);
          pagedir_set_swap(pd, owner->upage, swap_index);
        }
        break;
      case SPT_FRAME:
        /* Dirty frame pages get swapped out */
        if (is_dirty) {
          size_t swap_index = swap_out(kpage);
          pagedir_set_swap(pd, owner->upage, swap_index);
        }
        break;
      case SPT_SWAP:
        PANIC ("SWAP page must not be mapped");
      case SPT_INVALID:
        PANIC ("Evicted invalid SPT");
    }

    /* Get the next element before freeing the owner */
    struct list_elem *next_e = list_next (e);
    free (owner);
    e = next_e;
  }

  /* Reset the frame table entry */
  list_init (&victim->owners);

  return kpage;
}

/* Allocates a user frame, evicting if necessary. */
void *
frame_alloc (enum palloc_flags flags)
{
  /* Frame table is maintained only for user frames */
  ASSERT (flags & PAL_USER);

  lock_acquire(&frame_lock);

  /* Attempts to allocate a page from user pool */
  void *kpage = palloc_get_page (flags);

  /* If memory is full, we must evict a page to make room */
  if (kpage == NULL) {
    kpage = frame_evict();

    /* If a zeroed page is requested, we zero the evicted page */
    if (flags & PAL_ZERO) {
      memset(kpage, 0, PGSIZE);
    }
  }
  
  size_t frame_index = get_page_index(kpage);
  ASSERT(frame_index < get_user_pages());

  struct frame_table_entry *frame = &frame_table[frame_index];
  frame->pinned = false;
  list_init (&frame->owners);

  lock_release(&frame_lock);
  return kpage;   
}

/* Removes current process's mapping to kpage, freeing if last owner. */
void
frame_free (void *kpage)
{
  lock_acquire(&frame_lock);

  size_t frame_index = get_page_index(kpage);
  ASSERT(frame_index < get_user_pages());

  struct frame_table_entry *frame = &frame_table[frame_index];

  struct list_elem *e = list_begin(&frame->owners);
  struct frame_owner *found_owner = NULL;

  while (e != list_end(&frame->owners)) {
    struct frame_owner *owner = list_entry(e, struct frame_owner, elem);
    if (owner->process == thread_current()->process) {
      found_owner = owner;
      break;
    }
    e = list_next(e);
  }

  ASSERT (found_owner != NULL);

  list_remove(&found_owner->elem);
 
  /* If the frame is no longer referred to by any other process
     Free the page and clear the PTE to avoid repeated clear */
  if (list_empty(&frame->owners)) {
    palloc_free_page (kpage);
  }

  free(found_owner);

  lock_release(&frame_lock);
}

/* Installs mapping from upage to kpage and registers ownership. */
bool
frame_install_page (void *upage, void *kpage, bool writable)
{
  /* Install the mapping in the pagedir of the process */
  if (!install_page (upage, kpage, writable)) {
    return false;
  }
  struct frame_owner *owner =
    (struct frame_owner *) malloc (sizeof (struct frame_owner));
  if (owner == NULL) {
    return false;
  }
  owner->upage = upage;
  owner->process = thread_current()->process;

  lock_acquire(&frame_lock);

  /* The frame must have been allocated before any calls to install */
  size_t frame_index = get_page_index(kpage);
  struct frame_table_entry *frame = &frame_table[frame_index];
  list_push_front (&frame->owners, &owner->elem);
  
  lock_release(&frame_lock);
  return true;
}
