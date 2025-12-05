#include "threads/vaddr.h"
#include "vm/mmap.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/thread.h"
#include "userprog/fd_table.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

/* Initializes an mmap table. */
void
mmap_table_init (struct mmap_table *mmap_table)
{
  list_init (&mmap_table->list);
}

/* Destroys all mappings in the table and frees resources. */
void
mmap_table_destroy (struct mmap_table *mmap_table)
{
  struct list *list = &mmap_table->list;
  while (!list_empty (list)) {
    struct list_elem *e = list_front (list);
    struct mmap_entry *entry = list_entry (e, struct mmap_entry, elem);
    ASSERT (entry != NULL);
    /* mmap_free_entry removes the entry from the list and frees it */
    mmap_free_entry (entry);
  }
}

/* Returns next available map ID. */
static mapid_t
get_next_mapid (struct mmap_table *mmap_table)
{
  struct list *list = &mmap_table->list;
  /* As an invariant the last element in mmap_table has the largest id thus
     we choose the next id as this should not have been chosen already. The 
     first id is 0. */
  if (list_empty (list))
    return FIRST_MAP_ID;
  return list_entry (list_back (list), struct mmap_entry, elem)->id + 1;
}

/* Returns mmap entry with given ID, or NULL if not found. */
struct mmap_entry *
mmap_get_entry (struct mmap_table *mmap_table, mapid_t id)
{
  if (id < FIRST_MAP_ID)
    return NULL;
  for (struct list_elem *e = list_begin (&mmap_table->list);
       e != list_end (&mmap_table->list);
       e = list_next (e)) {
    struct mmap_entry *entry = list_entry (e, struct mmap_entry, elem);
    if (entry->id == id)
      return entry;
  }
  return NULL;
}

/* Creates a new mapping entry and returns its ID. */
mapid_t
mmap_new_entry (struct mmap_table *mmap_table, void *upage, struct file *file)
{
  struct list *list = &mmap_table->list;
  struct mmap_entry *entry = malloc (sizeof (struct mmap_entry));
  if (entry == NULL) {
    return MAP_FAILED;
  }
  mapid_t id = get_next_mapid (mmap_table);
  entry->id = id;
  entry->no_pages = 0;
  entry->upage = upage;
  entry->file = file;
  list_push_back (list, &entry->elem);
  return id;
}

/* Increments page count for the given mapping. */
void
mmap_increment_pages (struct mmap_table *mmap_table, mapid_t id)
{
  struct mmap_entry *entry = mmap_get_entry (mmap_table, id);
  entry->no_pages++;
}

/* Unmaps all pages and frees the mapping entry. */
void
mmap_free_entry (struct mmap_entry *entry)
{
  ASSERT (entry != NULL);
  void *upage = entry->upage;
  for (int i = 0; i < entry->no_pages; i++) {
    spt_remove_page (upage);
    upage += PGSIZE;
  }
  list_remove (&entry->elem);
  file_close (entry->file);
  free (entry);
}
