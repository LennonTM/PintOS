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

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  thread_current()->process->recover_flag = true;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  thread_current()->process->recover_flag = false;
  return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  thread_current()->process->recover_flag = true;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  thread_current()->process->recover_flag = false;
  return error_code != -1;
}

static void 
halt (void) NO_RETURN;

static void
handle_halt(struct intr_frame *f) {
  printf("Handler: handle_halt called\n");
}


static void 
exit (int status) NO_RETURN;

static void 
handle_exit (struct intr_frame *f) {
  printf("Handler: handle_exit  called\n");
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


static bool 
create (const char *file, unsigned initial_size);

static void
handle_create (struct intr_frame *f) {
  printf("Handler: handle_create  called\n");
}


static bool 
remove (const char *file);

static void
handle_remove (struct intr_frame *f) {
  printf("Handler: handle_remove  called\n");
}


static int 
open (const char *file);

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
  process_exit (PROC_ERR);
}

