#ifndef USERPROG_FD_TABLE_H
#define USERPROG_FD_TABLE_H

#include "list.h"
#include "filesys/file.h"

/* The first user file descriptor is 2 since 0 and 1 are used
   for the standard input / output */
#define USER_FIRST_FD 2

/* Struct for file descriptor table to abstract its
   implementation from the process */
struct fd_table
  {
    struct list list; /* List of fd entries */
  };

/* An entry in the fd_table
   Associates fd to the struct file */
struct fd_entry
  {
    int fd; /* File descriptor. */
    struct file* file; /* File which can be handled by file.c. */
    struct list_elem elem; /* List element for fd_table list. */
  };

void fd_table_init (struct fd_table* fd_table);
struct file* get_file (struct fd_table* fd_table, int fd);
int add_file (struct fd_table* fd_table, struct file* file_);
void remove_file (struct fd_table* fd_table, int fd);
void free_fd_table(struct fd_table* fd_table);

#endif /* userprog/fd_table.h */
