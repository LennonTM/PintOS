#include "userprog/fd_table.h"
#include "threads/malloc.h"

void
fd_table_init(struct fd_table* fd_table) {
  list_init(&fd_table->list);
}

/* Retrives the struct file of the file descriptor (fd) of the
   current process. NULL if no file with this fd is in the table */
struct file*
get_file (struct fd_table* fd_table, int fd) {
  struct list* list = &fd_table->list;
  /* Iterates through the fd_table of the current process, and gets the 
     struct file of file descriptor fd. */
  for (
      struct list_elem *e = list_begin (list); 
      e != list_end (list); 
      e = list_next (e))
  {
    struct fd_entry *entry = list_entry (e, struct fd_entry, elem);
    if (entry->fd == fd) {
      /* There is at most one file of a given fd */
      return entry->file;
    }
  }
  return NULL;
}

static int
get_next_fd (struct fd_table* fd_table) {
  struct list* list = &fd_table->list;
  /* As an invariant the last element in fd_table has the largest fd thus
     we choose the next fd as this should not have been chosen already. In the
     case where fd_table is empty the only file_descriptors are 0 / 1 for
     STDOUT / STDIN. */
  int fd = list_empty (list) ? USER_FIRST_FD :
    list_entry(list_back(list), struct fd_entry, elem)->fd + 1;
  return fd;
}

/* Adds file to the file descriptor table, returns the file descriptor that
   the file is stored under. */
int
add_file (struct fd_table* fd_table, struct file* file) {
  struct list* list = &fd_table->list;
  int fd = get_next_fd (fd_table);
  struct fd_entry* entry =  malloc (sizeof(struct fd_entry));
  entry->fd = fd;
  entry->file = file;
  list_push_back(list, &entry->elem);
  return fd;
}

/* Removes the file from the file descriptor table,
   frees the entry and closes the file */
void
remove_file (struct fd_table* fd_table, int fd) {
  struct list* list = &fd_table->list;
  /* Iterates through the fd_table of the current process
     and removes the entry corresponding to fd */
  for (
      struct list_elem *e = list_begin (list); 
      e != list_end (list); 
      e = list_next (e))
  {
    struct fd_entry *entry = list_entry (e, struct fd_entry, elem);
    if (entry->fd == fd) {
      list_remove (e);
      file_close(entry->file);
      free (entry);
      return;
    }
  }
}

/* Frees remaining file entries and closes their files */
void
free_fd_table(struct fd_table* fd_table) {
  struct list* list = &fd_table->list;
  /* Iterates through the fd_table of the current process,
     closing all files and removing them from the table */
  struct list_elem *e = list_begin (list); 
  while (e != list_end (list)) {
    struct fd_entry *entry = list_entry (e, struct fd_entry, elem);
    struct file *file_ = entry->file;
    file_close (file_);
    /* Remove list_elem from the list before freeing the entry */
    e = list_remove (e);
    free (entry);
  }
}

