#ifndef PAGE_H
#define PAGE_H

#include <inttypes.h>
#include <stdlib.h>
#include "lib/kernel/hash.h"
#include "lib/debug.h"
#include "filesys/file.h"

enum page_status {
  SPT_SWAP,   /* Page is stored in swap space */
  SPT_FILE,   /* Writable page from a file */
  SPT_EXEC,   /* Writable executable page to be lazy-loaded */
  SPT_FRAME,  /* Page is loaded in memory (if evicted -> SPT_SWAP) */
  SPT_SHARED, /* Shared read-only executable pages */
};

struct file_aux {
  struct file *file;        /* File to read page data from. */
  size_t ofs;               /* Byte offset within the file. */
  size_t page_read_bytes;   /* Bytes to read from file (rest is zeroed). */
};

struct swap_aux {
  size_t index;  /* Swap slot index. */
};

union spt_entry_aux {
  struct file_aux file;
  struct swap_aux swap;
};

struct spt_entry {
  void *upage;              /* User virtual address of the page. */
  bool writable;            /* True if page is writable. */
  enum page_status status;  /* Where/how the page is stored. */
  union spt_entry_aux aux;  /* Status-specific metadata. */
  struct hash_elem elem;
};

unsigned spt_hash (const struct hash_elem *p_, void *aux UNUSED);
bool spt_less (const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED);

void spt_record_file_page (struct hash *spt, struct file *file, off_t ofs,
                           uint8_t *upage, uint32_t page_read_bytes,
                           bool writable);
void spt_record_exec_page (struct hash *spt, struct file *file, off_t ofs,
                           uint8_t *upage, uint32_t page_read_bytes,
                           bool writable);
void spt_record_swap_page (struct hash *spt, uint8_t *upage, bool writable,
                           size_t swap_index);
void spt_record_frame_page (struct hash *spt, uint8_t *upage, bool writable);
bool spt_remove_entry (struct hash *spt, struct spt_entry *entry);
struct spt_entry *spt_get_entry (struct hash *spt, void *upage);
void spt_destroy (struct hash *spt);

bool spt_load_swap_page (struct spt_entry *spt_entry);
uint8_t *spt_load_file_page (struct spt_entry *spt_entry);
bool spt_load_shared_page (struct spt_entry *spt_entry);
void spt_remove_page (void *upage);

#endif 
