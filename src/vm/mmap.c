#include "threads/vaddr.h"
#include "vm/mmap.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "threads/thread.h"
#include "userprog/fd_table.h"

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
        /* Remove list_elem from the list before freeing the entry */
        e = list_remove (e);
        free_entry(mmap_table, entry->id);
    }
}

static mapid_t
get_next_mapid (struct mmap_table* mmap_table) {
  struct list* list = &mmap_table->list;
  /* As an invariant the last element in mmap_table has the largest id thus
     we choose the next id as this should not have been chosen already. The 
     first id is 0. */
  int id = list_empty (list) ? FIRST_MAP_ID :
    list_entry(list_back(list), struct mmap_entry, elem)->id + 1;
  return id;
}

/* Performs a linear search for entry with id of mapping. */
struct mmap_entry* get_entry(struct mmap_table* mmap_table, mapid_t mapping) {
    if (mapping < FIRST_MAP_ID) {
        return NULL;
    }
    for (
        struct list_elem *e = list_begin(&mmap_table->list); 
        e!= list_end(&mmap_table->list); 
        e = list_next(e)
    ) {
        struct mmap_entry * entry = list_entry (e, struct mmap_entry, elem);
        if (entry->id == mapping) {
            return entry;
        }
    }
    return NULL;
}

/* Creates new entry in table with a single page*/
mapid_t new_entry (struct mmap_table* mmap_table, void* upage, int fd) {
    struct list* list = &mmap_table->list;
    int id = get_next_mapid (mmap_table);
    struct mmap_entry* entry = malloc (sizeof(struct mmap_entry));
    entry->id = id;
    entry->no_pages = 1;
    entry->upage = upage;
    entry->fd = fd;
    list_push_back(list ,&entry->elem);
    return id;
}

/* Adds another page to the mapping by incrementing page_no */
void extend(struct mmap_table* mmap_table, mapid_t mapping) {
    struct list_elem *e = list_begin(&mmap_table->list);
    struct mmap_entry * entry = list_entry (e, struct mmap_entry, elem);
    entry->no_pages++;   
}
/* Iterates through the pages mapped at mapping, removes them from the 
   SPT table/page table and removes/frees the entry in the mmap_table. 
   Performs write backs to file if page is dirty. */
void free_entry(struct mmap_table* mmap_table, mapid_t mapping) {
    struct mmap_entry * entry = get_entry(mmap_table, mapping);
    ASSERT(entry != NULL);

    struct file* file = open(entry->fd);
    struct hash* spt = thread_current()->process->spt;
    uint32_t *pagedir = thread_current()->process->pagedir;

    void* upage = entry->upage;
    for (int i = 0; i < entry->no_pages; i++) {
        /* There are two cases either the page wasnt loaded or it was,
           if it wasn't loaded it is in SPT, otherwise it resides in page
           table */
        if (pagedir_get_page(pagedir, upage) == NULL) {
            remove_page(upage + i*PGSIZE);
            continue;
        }
        if (pagedir_is_dirty(pagedir, upage)) {
            write (entry->fd, upage, PGSIZE);
        }            
        pagedir_clear_page(pagedir, upage);
    }
    list_remove(&entry->elem);
    free(entry);
    close(entry->fd);
}