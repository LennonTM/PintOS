#ifndef USERPROG_PAGEDIR_H
#define USERPROG_PAGEDIR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

uint32_t *pagedir_create (void);
void pagedir_destroy (uint32_t *pd);
bool pagedir_set_page (uint32_t *pd, void *upage, void *kpage, bool rw);
void *pagedir_get_page (uint32_t *pd, const void *upage);
void pagedir_clear_page (uint32_t *pd, void *upage);
bool pagedir_is_dirty (uint32_t *pd, const void *upage);
void pagedir_set_dirty (uint32_t *pd, const void *upage, bool dirty);
bool pagedir_is_accessed (uint32_t *pd, const void *upage);
void pagedir_set_accessed (uint32_t *pd, const void *upage, bool accessed);
bool pagedir_is_writable (uint32_t *pd, const void *upage);
void pagedir_set_writable (uint32_t *pd, const void *upage, bool writable);
uint8_t pagedir_get_avl (uint32_t *pd, const void *upage);
void pagedir_set_avl (uint32_t *pd, const void *upage, uint8_t data);
void pagedir_activate (uint32_t *pd);
bool pagedir_create_pte (uint32_t *pd, const void *vpage, bool writable);
bool pagedir_set_swap (uint32_t *pd, void *upage, size_t swap_slot);
size_t pagedir_get_swap (uint32_t *pd, void *upage);

#endif /* userprog/pagedir.h */
