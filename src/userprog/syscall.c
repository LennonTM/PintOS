#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

/* Type of a specific system call handler helper function
   Each handler takes the frame, gets the arguments
   and calls corresponding system call implementation */
typedef void (*handle_syscall)(uint8_t *esp, uint32_t *eax);

static void NO_RETURN exit (int status);

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

#define BYTE_SIZE 8
#define WORD_BYTES (sizeof(uint32_t)/sizeof(uint8_t))

static bool
get_user_word (const uint8_t *uaddr, uint32_t *result) {
  *result = 0;
  for (size_t i = 0; i < WORD_BYTES; i++) {
    const uint8_t *byte_addr = uaddr + i;
    int byte = get_user(byte_addr);
    if ((void *) byte_addr >= PHYS_BASE || byte == -1) {
      return false;
    }
    *result |= (byte << (i * sizeof(uint8_t) * 8));
  }
  return true;
}

static uint32_t
parse_argument (uint8_t ** uaddr) {
  uint32_t result;
  if (!get_user_word(*uaddr, &result)) {
    exit(-1);
    NOT_REACHED ();
  }
  *uaddr += WORD_BYTES;
  return result;
}

/* Terminates PintOS by calling shutdown_power_off */
static void 
halt (void) {
  shutdown_power_off();
}

static void
handle_halt(uint8_t *esp UNUSED, uint32_t *eax UNUSED) {
  printf("Handler: handle_halt called\n");
  halt();
}


static void NO_RETURN
exit (int status) {
  char *process_name = thread_current()->name;
  printf ("%s: exit(%d)\n", process_name, status);
  process_exit (status);
}

static void 
handle_exit (uint8_t *esp, uint32_t *eax UNUSED) {
  int status = (int) parse_argument(&esp);
  exit(status);
}

static pid_t 
exec (const char *file) {

}

static void
handle_exec (uint8_t *esp, uint32_t *eax UNUSED) {
  char *file = (char *) parse_argument(&esp);
  pid_t res = exec(file);
  *eax = res;
  printf("Handler: handle_exec  called\n");
}


static int 
wait (pid_t wait_pid) {

}

static void
handle_wait (uint8_t *esp, uint32_t *eax) {
  pid_t wait_pid = (pid_t) parse_argument(&esp);
  *eax = wait(wait_pid);
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
handle_create (uint8_t *esp, uint32_t *eax) {
  char *file = (char *) parse_argument(&esp);
  unsigned initial_size = (unsigned) parse_argument(&esp);
  *eax = create(file, initial_size);
  printf("Handler: handle_create  called\n");
}

/* Deletes the file called file. Returns true if successful, false otherwise.*/
static bool 
remove (const char *file) {
  return filesys_remove(file);
}

static void
handle_remove (uint8_t *esp, uint32_t *eax) {
  char *file = (char *) parse_argument(&esp);
  *eax = remove(file);
  printf("Handler: handle_remove  called\n");
}


static int 
open (const char *file) {
  printf("Handler: handle_open  called\n");
}

static void
handle_open (uint8_t *esp, uint32_t *eax) {
  char *file = (char *) parse_argument(&esp);
  *eax = open(file);
}


static int 
filesize (int fd) {
  printf("Handler: handle_filesize  called\n");
}

static void
handle_filesize (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  *eax = filesize(fd);
}


static int 
read (int fd, void *buffer, unsigned length);

static void
handle_read (uint8_t *esp, uint32_t *eax) {
  printf("Handler: handle_read  called\n");
}


static int 
write (int fd, const void *buffer, unsigned length) {
  if (fd == 1) {
    /* Write to standard output */
    putbuf(buffer, length);
  } else {
    printf("Writing to file fd: %d\n", fd);
  }
}

static void
handle_write (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  const void *buffer = (const void *) parse_argument(&esp);
  unsigned length = (unsigned) parse_argument(&esp);
  *eax = write(fd, buffer, length);
}


static void 
seek (int fd, unsigned position);

static void
handle_seek (uint8_t *esp, uint32_t *eax) {
  printf("Handler: handle_seek  called\n");
}


static unsigned 
tell (int fd);

static void
handle_tell (uint8_t *esp, uint32_t *eax) {
  printf("Handler: handle_tell  called\n");
}


static void 
close (int fd);

static void
handle_close (uint8_t *esp, uint32_t *eax) {
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
  uint8_t *esp_cpy = f->esp;
  uint32_t syscall_num = (uint32_t) parse_argument(&esp_cpy);
  handlers[syscall_num](esp_cpy, &f->eax);
}

