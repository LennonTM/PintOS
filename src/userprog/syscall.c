#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/fd_table.h"
#include "threads/interrupt.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "lib/stdio.h"

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
    exit(PROC_ERR);
    NOT_REACHED ();
  }
  *uaddr += WORD_BYTES;
  return result;
}

static bool
check_valid_string(const char *string) {
  const char *p = string;
  int byte;
  while ((void *) p < PHYS_BASE) {
    byte = get_user((const uint8_t *) p);
    if (byte == -1) {
      return false;
    }
    if (byte == '\0') {
      return true;
    }
    p++;
  }
  return false;
}

static bool
check_valid_buffer(char *buffer, unsigned length) {
  const char *p = buffer;
  int byte;
  const char *p_end = p + length;
  while ((void *) p < PHYS_BASE) {
    byte = get_user((const uint8_t *) p);
    if (byte == -1) {
      return false;
    }
    if (p >= p_end) {
      return true;
    }
    p++;
  }
  return false;
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
  process_exit (status);
}

static void 
handle_exit (uint8_t *esp, uint32_t *eax UNUSED) {
  int status = (int) parse_argument(&esp);
  exit(status);
}

/* Runs the executable whose name is given in cmd line, passing any given 
   arguments, and returns the new process’s program id (pid). 
   Must return pid -1, if the program cannot load or run for any reason. */
static pid_t 
exec (const char *cmd_line) {
  return process_execute(cmd_line);
}

static void
handle_exec (uint8_t *esp, uint32_t *eax UNUSED) {
  char *cmd_line = (char *) parse_argument(&esp);
  if (!check_valid_string(cmd_line)) {
    exit(PROC_ERR);
  }
  pid_t res = exec(cmd_line);
  *eax = res;
}


static int 
wait (pid_t wait_pid) {
  return process_wait((tid_t) wait_pid);
}

static void
handle_wait (uint8_t *esp, uint32_t *eax) {
  pid_t wait_pid = (pid_t) parse_argument(&esp);
  *eax = wait(wait_pid);
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
  if (!check_valid_string(file)) {
    exit(PROC_ERR);
  }
  *eax = create(file, initial_size);
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
}

/* Opens the file called file. Returns non-negative integer handle called
   file descriptor (fd) or -1 if file could not be opened. A fd of 1 or
   0 is reserved for the console. */
static int 
open (const char *file_name) {
  struct file* file = filesys_open(file_name);
  if (file == NULL)
    return -1;
  return add_file (&thread_current()->process->fd_table, file);
}

static void
handle_open (uint8_t *esp, uint32_t *eax) {
  char *file = (char *) parse_argument(&esp);
  if (!check_valid_string(file))
    exit(PROC_ERR);
  *eax = open(file);
}

/* Returns the size, in bytes, of the file open as fd. */
static int 
filesize (int fd) {
  ASSERT ((fd != STDIN_FILENO) && (fd != STDOUT_FILENO));
  struct file *file = get_file (&thread_current()->process->fd_table, fd);
  return file_length (file);
}

static void
handle_filesize (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  *eax = filesize(fd);
}

/* Reads size bytes from the file open as fd into buffer. Returns
   number of bytes actually read. Returns -1 if there is an error
   in getting open file.*/
static int 
read (int fd, void *buffer, unsigned length) {
  if (fd == STDOUT_FILENO) {
    return -1;
  }
  if (fd == STDIN_FILENO) {
    char* buffer_ = (char*) buffer;
    for (unsigned i = 0; i<length; i++) {
      *buffer_++ = input_getc();
    }
    return length;
  }
  else {
    struct file* file = get_file (&thread_current()->process->fd_table, fd);
    if (file == NULL)
      return -1;
    if (!check_valid_buffer(buffer, length)) {
      exit(PROC_ERR);
    }
    return file_read (file, buffer, length);
  }
}

static void
handle_read (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  void* buffer = (void*) parse_argument(&esp);
  unsigned length = (unsigned) parse_argument(&esp);
  *eax = read(fd, buffer, length);
}

#define MAX_WRITE_LENGTH 256

/* Writes size bytes from buffer to open file fd. Returns number of bytes
   actually written.*/
static int 
write (int fd, const void *buffer, unsigned length) {
  if (fd == STDIN_FILENO) {
    return -1;
  }
  if (fd == STDOUT_FILENO) {
    for (int char_left = length; char_left > 0; char_left -= MAX_WRITE_LENGTH)
    {
      size_t put_length = 
        (char_left < MAX_WRITE_LENGTH) ? char_left : MAX_WRITE_LENGTH;
      putbuf(buffer, put_length);
    }
    return length;
  }
  else {
    struct file* file = get_file (&thread_current()->process->fd_table, fd);
    if (file == NULL) {
      return -1;
    }
    if (!check_valid_buffer(buffer, length)) {
      exit(PROC_ERR);
    }
    return file_write(file, buffer, length);
  }
}

static void
handle_write (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  const void *buffer = (const void *) parse_argument(&esp);
  unsigned length = (unsigned) parse_argument(&esp);
  *eax = write(fd, buffer, length);
}

/* Changes the next byte to be read or written in open file fd to position. 
   Expressed in bytes from the beginning of the file. */
static void 
seek (int fd, unsigned position) {
  struct file* file = get_file (&thread_current()->process->fd_table, fd);
  file_seek (file, position);
}

static void
handle_seek (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  unsigned position = (unsigned) parse_argument(&esp);
  seek(fd, position);
}

/* Returns the position of the next byte to be read or written in open file
   fd, expressed in bytes from beginning of the file. */
static unsigned 
tell (int fd) {
  struct file* file = get_file (&thread_current()->process->fd_table, fd);
  return file_tell (file);
}

static void
handle_tell (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  *eax = tell(fd);
}

/* Removes file descriptor fd from the fd_table and closes its file. */
static void 
close (int fd) {
  if ((fd == STDIN_FILENO) || (fd == STDOUT_FILENO))
    return;
  remove_file (&thread_current()->process->fd_table, fd);
}

static void
handle_close (uint8_t *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  close(fd);
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
  if (syscall_num >= TOTAL_SYSCALLS) {
    exit(PROC_ERR);
  }
  handlers[syscall_num](esp_cpy, &f->eax);
}

