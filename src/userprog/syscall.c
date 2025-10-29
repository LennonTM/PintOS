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

static void
handle_halt(struct intr_frame *f) UNUSED;

static void 
halt (void) NO_RETURN UNUSED;


static void 
handle_exit (int status) NO_RETURN UNUSED;

static void 
exit (int status) NO_RETURN UNUSED;


static pid_t 
handle_exec (const char *file) UNUSED;

static pid_t 
exec (const char *file) UNUSED;


static int 
handle_wait (pid_t wait_pid) UNUSED;

static int 
wait (pid_t wait_pid) UNUSED;


static bool 
handle_create (const char *file, unsigned initial_size) UNUSED;

static bool 
create (const char *file, unsigned initial_size) UNUSED;


static bool 
handle_remove (const char *file) UNUSED;

static bool 
remove (const char *file) UNUSED;


static int 
handle_open (const char *file) UNUSED;

static int 
open (const char *file) UNUSED;


static int 
handle_filesize (int fd) UNUSED;

static int 
filesize (int fd) UNUSED;


static int 
handle_read (int fd, void *buffer, unsigned length) UNUSED;

static int 
read (int fd, void *buffer, unsigned length) UNUSED;


static int 
handle_write (int fd, const void *buffer, unsigned length) UNUSED;

static int 
write (int fd, const void *buffer, unsigned length) UNUSED;


static void 
handle_seek (int fd, unsigned position) UNUSED;

static void 
seek (int fd, unsigned position) UNUSED;


static unsigned 
handle_tell (int fd) UNUSED;

static unsigned 
tell (int fd) UNUSED;


static void 
handle_close (int fd) UNUSED;

static void 
close (int fd) UNUSED;

