#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "list.h"
#include "threads/synch.h"

#define PROC_SUCC (0)  /* Exit code for normal process termination. */
#define PROC_ERR (-1)  /* Exit code for erroneous process termination. */

#define WORD_BYTES (sizeof(uint32_t)/sizeof(uint8_t))
typedef int pid_t;

/* A user process, wrapped around a kernel thread. */
struct process
  {
    uint32_t *pagedir;      /* Process page directory. */
    struct thread *thread;  /* Process underlying kernel thread. */
    bool recover_flag;      /* Indicates whether the page fault
                               needs to be recovered from, without
                               causing kernel panic */
    struct list *fd_table;   /* List of file descriptors to file structs. */
    
    /* Each process is allocated a child_process_entry
       This allows communication with
       a waiting parent on exit */
    struct child_process_entry *entry; 
    /* Lists all children entries of this process 
       This allows communication with all children. */
    struct list child_entries; 
    pid_t pid;
  };



/* 3rd party to allow communication with waiting parent process */
struct child_process_entry
  {
    /* Process id of this (the child) process */
    pid_t pid;  

    /* Flag that is set when the parent dies or has finished waiting */
    bool parent_flag;
    /* Flag that is set when the child process dies */  
    bool self_flag;

    /* Performs synchronisation: the parent must wait until the child is dead */
    struct semaphore sema;
    /* Mutual access on setting and checking flags 
       in order to free this struct safely */       
    struct lock lock;           
    
    /* Value to pass to the parent */
    uint32_t return_value;
    
    /* An element in the parent's list of children */
    struct list_elem child_entry; 
  };

void root_process_init (void);
tid_t process_execute (const char *cmd_line);
int process_wait (tid_t);
void process_exit (int) NO_RETURN;
void process_activate (void);

#endif /* userprog/process.h */
