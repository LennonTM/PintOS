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

/* Lock prevents race conditions when accessing the frame table
for allocation and eviction */
struct lock frame_lock;

/* Used to iterate through frame table to find a 
victim frame to evict */
static size_t eviction_search_index = 0;

/* frame_table_entry is an array of all frame_table entries */
static struct frame_table_entry *frame_table = NULL;

/* Initialises frame_table_entry array */
void
frame_table_init (void) {
  size_t user_pages = get_user_pages();
  /* Set up frame table */
  frame_table = malloc(sizeof(struct frame_table_entry) * user_pages);
  if (frame_table == NULL) 
    PANIC("Failed to malloc frame table!");

  lock_init(&frame_lock);
}

/* Selects a frame to evict to swap disk, and returns the freed kpage address */
static void *
frame_evict (void) {
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
    struct spt_entry *spt_entry =
      spt_get_entry(&owner->process->spt, owner->upage);

    /* The next time a process touches the address, it will trigger a
    page fault, so the page can be loaded again */
    pagedir_clear_page(owner->process->pagedir, owner->upage);
    
    bool is_dirty = pagedir_is_dirty(owner->process->pagedir, owner->upage);

    ASSERT (spt_entry != NULL);
    if (spt_entry != NULL) {
      switch (spt_entry->status) {
        case FILE:
          /* If a file page is dirty, write it to the file */
          if (is_dirty) {
            /* If file is denied writes, then file_write will not
              modify the file, which is a desired behaviour */
            struct file_aux *f = &spt_entry->aux.file;
            file_write_at (f->file, kpage, f->page_read_bytes, f->ofs);
          }
          else if (!spt_entry->writable) {
            struct file_aux *f = &spt_entry->aux.file;
            unlink_shared_entry (f->file, f->ofs, spt_entry);
          }
          /* Otherwise the page is cleared from memory, but
             spt entry is kept to load it again later */
          break;
        case W_EXEC:
          if (is_dirty) {
            size_t swap_index = swap_out(kpage);
            spt_entry->status = SWAP;
            spt_entry->aux.swap.index = swap_index;
          }
          break;
        case SWAP:
          PANIC ("SWAP page must not be mapped");
        case FRAME:
          if (is_dirty) {
            size_t swap_index = swap_out(kpage);
            spt_entry->status = SWAP;
            spt_entry->aux.swap.index = swap_index;
          }
      }
    }
    else if (is_dirty) {
      /* Otherwise, the page is a stack page */
      size_t swap_index = swap_out(kpage);
      spt_record_swap_page (&owner->process->spt, owner->upage,
                            true, swap_index);
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

/* Allocates a page using palloc_get_page
   the corresponding frame_table_entry will be used after */
void *
frame_alloc (enum palloc_flags flags) {
  /* Frame table is maintained only for user frames */
  ASSERT (flags & PAL_USER);

  bool lock_held = lock_held_by_current_thread(&frame_lock);

  /* page_fault aquires the lock to safely read the SPT status.
  frame_alloc must check that the thread does not already hold
  the lock before trying to aquire it to prevent a deadlock.
  When the thread holds the lock, we can safely modify the
  global Frame Table. */
  if(!lock_held) lock_acquire(&frame_lock);

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

  if (!lock_held) lock_release(&frame_lock);
  return kpage;   
}

/* Frees a page using palloc_free_page
   the corresponding frame_table_entry will be unused after */
void
frame_free (void *kpage) {
  bool lock_held = lock_held_by_current_thread(&frame_lock);
  if (!lock_held) lock_acquire(&frame_lock);

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

  if (found_owner != NULL) {
    list_remove(&found_owner->elem);
    free(found_owner);
  }
  
  /* Let freeing the pages be handled by pagedir_destroy */
  // if (list_empty(&frame->owners)) {
  //   palloc_free_page(kpage);
  // }

  if (!lock_held) lock_release(&frame_lock);
}

/* Maps user virtual address UPAGE to a frame at KPAGE
   by calling install_page
   Sets owner and upage members of a frame corresponding to kpage */
bool
frame_install_page(void *upage, void *kpage, bool writable) {
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

  bool lock_held = lock_held_by_current_thread(&frame_lock);
  if (!lock_held) lock_acquire(&frame_lock);

  /* The frame must have been allocated before any calls to install */
  size_t frame_index = get_page_index(kpage);
  struct frame_table_entry *frame = &frame_table[frame_index];
  list_push_front (&frame->owners, &owner->elem);
  
  if (!lock_held) lock_release(&frame_lock);
  return true;
}
