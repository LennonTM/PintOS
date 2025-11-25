#include "threads/vaddr.h"
#include "vm/mmap.h"
#include "vm/page.h"

void mmap_table_init (struct mmap_table* mmap_table) {
    list_init (&mmap_table->list);
}
void free_mmap_table(struct mmap_table* mmap_table) {
    struct list* list = &mmap_table->list;
    /* Iterates through the mmap_table of the current process,
       removing the pages of each mapping and removing the mapping struct
       from the table.*/
    struct list_elem *e = list_begin (list); 
    while (e != list_end (list)) {
        struct mmap_entry *entry = list_entry (e, struct mmap_entry, elem);
        void* upage = entry->upage;
        for (int i = 0; i < entry->no_pages; i++) {
            remove_page(upage + i*PGSIZE);
        }
        /* Remove list_elem from the list before freeing the entry */
        e = list_remove (e);
        free (entry);
    }
}

static mapid_t
get_next_mapid (struct mmap_table* mmap_table) {
  struct list* list = &mmap_table->list;
  /* As an invariant the last element in mmap_table has the largest id thus
     we choose the next id as this should not have been chosen already. The 
     first id is 0. */
  int id = list_empty (list) ? 0 :
    list_entry(list_back(list), struct mmap_entry, elem)->id + 1;
  return id;
}

/* Creates new entry in table with a single page*/
mapid_t new_entry (struct mmap_table* mmap_table, void* upage) {
    struct list* list = &mmap_table->list;
    int id = get_next_mapid (mmap_table);
    struct mmap_entry* entry = malloc (sizeof(struct mmap_entry));
    entry->id = id;
    entry->no_pages = 1;
    entry->upage = upage;
    list_push_back(list ,&entry->elem);
    return id;
}

/* Adds another page to the mapping by incrementing page_no */
void extend(struct mmap_table* mmap_table, mapid_t mapping) {
    struct list_elem *e = list_begin(&mmap_table->list);
    struct mmap_entry * entry = list_entry (e, struct mmap_entry, elem);
    entry->no_pages++;   
}
/* Iterates through the pages mapped at mapping, removes them from the SPT table 
   and removes the entry in the mmap_table. */
void munmap(struct mmap_table* mmap_table, mapid_t mapping) {
    struct list_elem *e = list_begin(&mmap_table->list);
    struct mmap_entry * entry = list_entry (e, struct mmap_entry, elem);
    for (int i = 0; i < entry->no_pages; i++) {
            remove_page(entry->upage + i*PGSIZE);
    }
}