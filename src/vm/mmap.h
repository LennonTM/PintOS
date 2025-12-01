#ifndef MMAP_H
#define MMAP_H

#include <list.h>
/* Map region identifier. */
typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)
#define FIRST_MAP_ID 0

struct mmap_table
  {
    struct list list; /* List of fd entries */
  };

struct mmap_entry {
    mapid_t id; /* Unique indentifier of the memory mapping within a process. */
    struct file* file;     /* File pointer of the memory mapping. */
    void* upage; /* The virtual address of the first page of the mapping. */
    int no_pages; /* The number of pages represented by the mapping. */
    struct list_elem elem;
};


void mmap_table_init (struct mmap_table* mmap_table);
void free_mmap_table(struct mmap_table* mmap_table);

/* Performs a linear search for entry with id of mapping. */
struct mmap_entry* mmap_get_entry(struct mmap_table* mmap_table, mapid_t mapping);

/* Creates new entry in table with a single page*/
mapid_t mmap_new_entry (
  struct mmap_table* mmap_table, 
  void* upage, 
  struct file* file
);
/* Adds another page to the mapping by incrementing page_no */
void mmap_increment_pages_no(struct mmap_table* mmap_table, mapid_t mapping);
/* Iterates through the pages mapped in entry, removes them from the 
   SPT table/page table and removes/frees the entry in the mmap_table. 
   Performs write backs to file if page is dirty. */
void mmap_free_entry(struct mmap_entry * entry);


#endif /* vm/mmap.h */

