#ifndef PAGE_H
#define PAGE_H

#include <inttypes.h>
#include <stdlib.h>
#include "lib/kernel/hash.h"
#include "lib/debug.h"
#include "filesys/file.h"

enum page_status {
  SPT_INVALID, /* Default case for unset page status */
  SPT_SWAP,   /* Page is stored in swap space */
  SPT_FILE,   /* Writable page from a file */
  SPT_EXEC,   /* Writable executable page to be lazy-loaded */
  SPT_FRAME,  /* Page is loaded in memory (if evicted -> SPT_SWAP) */
  SPT_SHARED, /* Shared read-only executable pages */
};

struct spt_entry {
  void *upage;              /* User virtual address of the page. */
  struct file *file;        /* File to read page data from. */
  size_t ofs;               /* Byte offset within the file. */
  size_t page_read_bytes;   /* Bytes to read from file (rest is zeroed). */
  struct hash_elem elem;
};

unsigned spt_hash (const struct hash_elem *p_, void *aux UNUSED);
bool spt_less (const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED);

void spt_record_page (struct hash *spt, struct file *file, off_t ofs,
                      uint8_t *upage, uint32_t page_read_bytes,
                      bool writable, enum page_status status);
bool spt_remove_entry (struct hash *spt, struct spt_entry *entry);
struct spt_entry *spt_get_entry (struct hash *spt, void *upage);
void spt_destroy (struct hash *spt);

bool spt_load_swap_page (void *upage);
uint8_t *spt_load_file_page (struct spt_entry *spt_entry);
bool spt_load_shared_page (struct spt_entry *spt_entry);
void spt_remove_page (void *upage);

enum page_status get_page_status (const void *upage);
void set_page_status (const void *upage, enum page_status status);

#endif 
