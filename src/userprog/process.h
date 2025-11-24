#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "list.h"
#include "threads/synch.h"
#include "userprog/fd_table.h"
#include "hash.h"

#define PROC_SUCC (0)  /* Exit code for normal process termination. */
#define PROC_ERR (-1)  /* Exit code for erroneous process termination. */

#define WORD_BYTES (sizeof(uint32_t)/sizeof(uint8_t))
#define BYTE_SIZE sizeof(uint8_t *)
typedef int pid_t;

/* A user process, wrapped around a kernel thread. */
struct process
  {
    uint32_t *pagedir;      /* Process page directory. */
    struct hash spt;        /* Supplementary page table */
    struct thread *thread;  /* Process underlying kernel thread. */
    bool recover_flag;      /* Indicates whether the page fault
                               needs to be recovered from, without
                               causing kernel panic */
    void *esp;              /* Saved stack pointer for interrupts */
    struct fd_table fd_table;   /* Associates file descriptors
                                   to file structs opened by the user. */
    
    /* Each process is allocated a child_process_entry
       This allows communication with
       a waiting parent on exit */
    struct child_to_parent_entry *entry; 
    /* Lists all children entries of this process 
       This allows communication with all children. */
    struct list child_entries; 

    struct file *executable_file;
  };



/* 3rd party struct to allow parent process
   to wait for its children, and for a child process
   to communicate its exit code to the parent
   It represents child to parent link.

   This struct is always stored on the heap
   and gets destroyed only when both child and parent
   are finished with it */
struct child_to_parent_entry
  {
    /* pid of the child process */
    pid_t pid;

    /* Flag gets set when the parent dies or finishes waiting */
    bool parent_finished;
    /* Flag gets set when the child process dies */
    bool child_finished;
    /* Flag that indicates whether child process loaded successfully
       allows the parent to verify whether the load was successful or not */
    bool loading_succeeded;

    /* Used for synchronisation in 2 cases:
       - parent waiting for a child to finish
       - parent waiting for a child to attempt to load */
    struct semaphore sema;
    /* Mutual access on setting and checking flags
       in order to free this struct safely */
    struct lock lock;
 
    /* Value to pass to the parent */
    uint32_t return_value;

    /* An element in the parent's list of children */
    struct list_elem child_elem; 
  };

void root_process_init (void);
tid_t process_execute (const char *cmd_line);
int process_wait (tid_t);
void process_exit (int) NO_RETURN;
void process_activate (void);

bool load_page_from_file (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t page_read_bytes, uint32_t page_zero_bytes,
                          bool writable);
bool load_page_zeroing (uint8_t *upage, bool writable);
#endif /* userprog/process.h */
