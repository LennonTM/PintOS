#ifndef MMAP_H
#define MMAP_H

#include <list.h>

typedef int mapid_t;
#define MAP_FAILED ((mapid_t) -1)
#define FIRST_MAP_ID 0

struct mmap_table {
  struct list list;  /* List of mmap_entry structs. */
};

struct mmap_entry {
  mapid_t id;           /* Unique mapping identifier within process. */
  struct file *file;    /* Mapped file. */
  void *upage;          /* Start address of first mapped page. */
  int no_pages;         /* Number of pages in mapping. */
  struct list_elem elem;
};


void mmap_table_init (struct mmap_table *mmap_table);
void mmap_table_destroy (struct mmap_table *mmap_table);
struct mmap_entry *mmap_get_entry (struct mmap_table *mmap_table, mapid_t id);
mapid_t mmap_new_entry (struct mmap_table *mmap_table, void *upage,
                        struct file *file);
void mmap_increment_pages (struct mmap_table *mmap_table, mapid_t id);
void mmap_free_entry (struct mmap_entry *entry);


#endif /* vm/mmap.h */

