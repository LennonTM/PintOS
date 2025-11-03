#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "list.h"

#define PROC_SUCC (0)  /* Exit code for normal process termination. */
#define PROC_ERR (-1)  /* Exit code for erroneous process termination. */

/* A entry in the fd_table. */
struct fd_entry
  {
    int fd; /* File descriptor. */
    struct file* file; /* File which can be handled by file.c. */
    struct list_elem elem; /* List element for doubly-linked td_table list. */
  };

/* A user process, wrapped around a kernel thread. */
struct process
  {
    uint32_t *pagedir;      /* Process page directory. */
    struct thread *thread;  /* Process underlying kernel thread. */
    struct list *fd_table;   /* List of file descriptors to file structs. */
  };

void root_process_init (void);
tid_t process_execute (const char *cmd_line);
int process_wait (tid_t);
void process_exit (int) NO_RETURN;
void process_activate (void);

#endif /* userprog/process.h */
