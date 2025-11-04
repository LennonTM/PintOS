#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/filesys.h"

/* A entry in the fd_table. */
struct fd_entry
  {
    int fd; /* File descriptor. */
    struct file* file; /* File which can be handled by file.c. */
    struct list_elem elem; /* List element for doubly-linked td_table list. */
  };


static void syscall_handler (struct intr_frame *);

/* Type of a specific system call handler helper function
   Each handler takes the frame, gets the arguments
   and calls corresponding system call implementation */
typedef void (*handle_syscall)(struct intr_frame *f);


/* Terminates PintOS by calling shutdown_power_off */
static void 
halt (void) {
  shutdown_power_off();
}

static void
handle_halt(struct intr_frame *f) {
  printf("Handler: handle_halt called\n");
  halt();
}


static void NO_RETURN
exit (int status) {
  char *process_name = thread_current()->name;
  printf ("%s: exit(%d)\n", process_name, status);
  process_exit (PROC_SUCC);
}

static void 
handle_exit (struct intr_frame *f) {
  printf("Handler: handle_exit  called\n");
  exit(-1);
}

static pid_t 
exec (const char *file);

static void
handle_exec (struct intr_frame *f) {
  printf("Handler: handle_exec  called\n");
}


static int 
wait (pid_t wait_pid);

static void
handle_wait (struct intr_frame *f) {
  printf("Handler: handle_wait  called\n");
}

/* Creates a new file called file initially initial_size bytes in size.
   Returns ture if successful, false otherwise. Creating a new file does
   not open it. */
static bool 
create (const char *file, unsigned initial_size) {
  return filesys_create(file, initial_size);
}

static void
handle_create (struct intr_frame *f) {
  printf("Handler: handle_create  called\n");
}

/* Deletes the file called file. Returns true if successful, false otherwise.*/
static bool 
remove (const char *file) {
  return filesys_remove(file);
}

static void
handle_remove (struct intr_frame *f) {
  printf("Handler: handle_remove  called\n");
}

#ifdef USERPROG

/* The first user file descriptor is 2 since 0 and 1 are used
   for the console. */
#define USER_FIRST_FD 2;

/* Retrives the struct file of the file descriptor (fd) of the 
   current process.*/
static struct file*
get_file (int fd) {
  struct file* to_return;
  struct list* fd_table = thread_current ()->process->fd_table;
  /* Iterates through the fd_table of the current process, and gets the 
     struct file of file descriptor fd. */
  for (
      struct list_elem *e = list_begin (fd_table); 
      e != list_end (fd_table); 
      e = list_next (e))
  {
    struct fd_entry *entry = list_entry (e, struct fd_entry, elem);
    if (entry->fd == fd) {
      /* As an invariant there should be only one file of a given fd. */
      ASSERT (to_return == NULL);
      to_return = entry;
    }
  }
  ASSERT (to_return != NULL);
  return to_return;
}

/* Adds file to the file descriptor table, returns the file descriptor that
   the file is stored under. */
static int
add_file (struct file* file_) {
  struct list* fd_table = thread_current ()->process->fd_table;
  /* As an invariant the last element in fd_table has the largest fd thus
     we choose the next fd as this should not have been chosen already. In the
     case where fd_table is empty the only file_descriptors are 0/1 for
     the console. */
  int fd = ?(list_empty (fd_table)) USER_FIRST_FD : 
    list_entry(list_back(fd_table), struct fd_entry, elem)->fd + 1;
  struct fd_entry* entry =  malloc (sizeof(struct fd_entry));
  entry->fd = fd;
  entry->file = file_;
  list_push_back(fd_table, &entry->elem);
  return fd;
}

#endif

/* Opens the file called file. Returns non-negative integer handle called
   file descriptor (fd) or -1 if file could not be opened. A fd of 1 or
   0 is reserved for the console. */
static int 
open (const char *file) {
  struct file* file_ = filesys_open(file);
  if (file_ == NULL)
    return -1;
  return add_file (file_);
}


static void
handle_open (struct intr_frame *f) {
  printf("Handler: handle_open  called\n");
}


static int 
filesize (int fd);

static void
handle_filesize (struct intr_frame *f) {
  printf("Handler: handle_filesize  called\n");
}


static int 
read (int fd, void *buffer, unsigned length);

static void
handle_read (struct intr_frame *f) {
  printf("Handler: handle_read  called\n");
}


static int 
write (int fd, const void *buffer, unsigned length);

static void
handle_write (struct intr_frame *f) {
  printf("Handler: handle_write  called\n");
}


static void 
seek (int fd, unsigned position);

static void
handle_seek (struct intr_frame *f) {
  printf("Handler: handle_seek  called\n");
}


static unsigned 
tell (int fd);

static void
handle_tell (struct intr_frame *f) {
  printf("Handler: handle_tell  called\n");
}


static void 
close (int fd);

static void
handle_close (struct intr_frame *f) {
  printf("Handler: handle_close  called\n");
}

#define TOTAL_SYSCALLS 13

static handle_syscall handlers[TOTAL_SYSCALLS] = {
  [SYS_HALT]=&handle_halt,
  [SYS_EXIT]=&handle_exit,
  [SYS_EXEC]=&handle_exec,
  [SYS_WAIT]=&handle_wait,
  [SYS_CREATE]=&handle_create,
  [SYS_REMOVE]=&handle_remove,
  [SYS_OPEN]=&handle_open,
  [SYS_FILESIZE]=&handle_filesize,
  [SYS_READ]=&handle_read,
  [SYS_WRITE]=&handle_write,
  [SYS_SEEK]=&handle_seek,
  [SYS_TELL]=&handle_tell,
  [SYS_CLOSE]=&handle_close,
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t syscall_num = *(uint32_t*)f->esp;
  printf ("system call: %d\n", syscall_num);
  handlers[syscall_num](f);
}

