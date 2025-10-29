#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include <stdio.h>
#include <syscall-nr.h>

static void syscall_handler (struct intr_frame *);

/* Type of a specific system call handler helper function
   Each handler takes the frame, gets the arguments
   and calls corresponding system call implementation */
typedef void (*handle_syscall)(struct intr_frame *f);

static void 
halt (void) NO_RETURN UNUSED;

static void
handle_halt(struct intr_frame *f) UNUSED;


static void 
exit (int status) NO_RETURN UNUSED;

static void 
handle_exit (struct intr_frame *f) NO_RETURN UNUSED;


static pid_t 
exec (const char *file) UNUSED;

static void
handle_exec (struct intr_frame *f) UNUSED;


static int 
wait (pid_t wait_pid) UNUSED;

static void
handle_wait (struct intr_frame *f) UNUSED;


static bool 
create (const char *file, unsigned initial_size) UNUSED;

static void
handle_create (struct intr_frame *f) UNUSED;


static bool 
remove (const char *file) UNUSED;

static void
handle_remove (struct intr_frame *f) UNUSED;


static int 
open (const char *file) UNUSED;

static void
handle_open (struct intr_frame *f) UNUSED;


static int 
filesize (int fd) UNUSED;

static void
handle_filesize (struct intr_frame *f) UNUSED;


static int 
read (int fd, void *buffer, unsigned length) UNUSED;

static void
handle_read (struct intr_frame *f) UNUSED;


static int 
write (int fd, const void *buffer, unsigned length) UNUSED;

static void
handle_write (struct intr_frame *f) UNUSED;


static void 
seek (int fd, unsigned position) UNUSED;

static void
handle_seek (struct intr_frame *f) UNUSED;


static unsigned 
tell (int fd) UNUSED;

static void
handle_tell (struct intr_frame *f) UNUSED;


static void 
close (int fd) UNUSED;

static void
handle_close (struct intr_frame *f) UNUSED;

#define TOTAL_SYSCALLS 13

static handle_syscall handlers[TOTAL_SYSCALLS] = {
  &handle_halt,
  &handle_exit,
  &handle_exec,
  &handle_wait,
  &handle_create,
  &handle_remove,
  &handle_open,
  &handle_filesize,
  &handle_read,
  &handle_write,
  &handle_seek,
  &handle_tell,
  &handle_close,
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  uint32_t syscall_num = *(uint32_t*)f->esp;
  printf ("system call: %d\n", syscall_num);
  process_exit (PROC_ERR);
}
