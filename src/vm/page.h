#ifndef PAGE_H
#define PAGE_H

#include <inttypes.h>
#include <stdlib.h>
#include "lib/kernel/hash.h"
#include "lib/debug.h"
#include "filesys/file.h"

/* Every page is either in a frame, in the swap partition or in the file system. 
   It could also be an all-zero page. */
enum page_status {
  SWAP,
  FILE,
  SPT_EXEC, /* Writable executable page to be lazy-loaded */
  FRAME,
  SPT_SHARED, /* Shared read-only executable pages */
};

struct file_aux {
  struct file* file;        /* Pointer to the struct file. */
  size_t ofs;               /* The number of bytes offset within the file. */
  size_t page_read_bytes;   /* Number of bytes to read from the file. */
};

struct swap_aux {
  size_t index; /* Index within the swap disk. */
};

struct frame_aux {
  void *kpage; /* Kernel virtual address of the frame */
};

/* We use a union to reduce size of struct when using mutually exclusive
   meta data between different locations page could be stored. */
union spt_entry_aux {
  struct file_aux file;
  struct swap_aux swap;
  struct frame_aux frame;
};

/* Entry to the Supplementary Page Table. */
struct spt_entry {
  void *upage; /* User virtual address of the page */

  /* Redundant, since this information can be stored in PTE
     TODO Remove in the future and record in PTE */
  bool writable; /* Is user page writable */

  enum page_status status; /* Indicates how the page should be handled */

  union spt_entry_aux aux; /* Meta data for the spt entry */

  struct hash_elem elem;
};

/* Returns a hash value for spt_entry p. */
unsigned
spt_hash (const struct hash_elem *p_, void *aux UNUSED);

/* Returns true if spt_entry a precedes spt_entry b. */
bool
spt_less (const struct hash_elem *a_, const struct hash_elem *b_,
void *aux UNUSED);

void spt_record_file_page (struct hash *spt, struct file *file, off_t ofs,
                           uint8_t *upage, uint32_t page_read_bytes,
                           uint32_t page_zero_bytes, bool writable);
void spt_record_exec_page (struct hash *spt, struct file *file, off_t ofs,
                           uint8_t *upage, uint32_t page_read_bytes,
                           uint32_t page_zero_bytes, bool writable);
void spt_record_swap_page (struct hash *spt, uint8_t *upage, bool writable,
                           size_t swap_index);
void spt_record_frame_page (struct hash *spt, uint8_t *upage, bool writable,
                            void *kpage);
bool spt_remove_entry (struct hash *spt, struct spt_entry *entry);
struct spt_entry *spt_get_entry (struct hash *spt, void *upage);
void spt_destroy (struct hash *spt);
bool spt_load_file_page (struct spt_entry* spt_entry);
bool spt_load_shared_page (struct spt_entry* spt_entry);
void spt_share_entry (struct spt_entry *spt_entry, struct list *shared_list);
void spt_remove_page (void* upage);

#endif 
