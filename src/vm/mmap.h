#ifndef MMAP_H
#define MMAP_H

#include <list.h>
/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)

struct mmap_table
  {
    struct list list; /* List of fd entries */
  };

struct mmap_entry {
    mapid_t id; /* Unique indentifier of the memory mapping within a process. */
    void* upage; /* The virtual address of the first page of the mapping. */
    int no_pages; /* The number of pages represented by the mapping. */
    struct list_elem elem;
};


void mmap_table_init (struct mmap_table* mmap_table);
void free_mmap_table(struct mmap_table* mmap_table);

/* Creates new entry in table with a single page*/
mapid_t new_entry (struct mmap_table* mmap_table, void* upage);
/* Adds another page to the mapping by incrementing page_no */
void extend(struct mmap_table* mmap_table, mapid_t mapping);
/* Iterates through the pages mapped at mapping, removes them from the SPT table 
   and removes the entry in the mmap_table. */
void munmap(struct mmap_table* mmap_table, mapid_t mapping);


#endif /* vm/mmap.h */

