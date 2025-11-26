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
#include "vm/page.h"
#include "vm/mmap.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

/* Type of a specific system call handler helper function
   Each handler takes the frame, gets the arguments
   and calls corresponding system call implementation */
typedef void (*handle_syscall)(void *esp, uint32_t *eax);

#define KERNEL_BUF_SIZE 256
#define min(x, y) ((x) < (y) ? (x) : (y))
#define CONTROLLED_PAGE_FAULT -1

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
  return error_code != CONTROLLED_PAGE_FAULT;
}

/* Copies data of specified length from kernel spcae buffer to
   user space buffer.
   returns false if invalid user memory is accessed */
static bool
copy_to_user_buf (const void *kernel_buf_, void *user_buf_, size_t length) {
  /* Verify that the buffer is within user space */
  if (!is_user_vaddr(user_buf_ + length)) {
    return false;
  }
  const uint8_t *kernel_buf = (const uint8_t *) kernel_buf_;
  uint8_t *user_buf = (uint8_t *) user_buf_;
  for (size_t i = 0; i < length; i++) {
    if (!put_user(user_buf, *kernel_buf)) {
      return false;
    }
    user_buf++;
    kernel_buf++;
  }
  return true;
}

/* Copies data of specified length from user spcae buffer to
   kernel space buffer.
   return false if user memory is invalid at some point */
static bool
copy_from_user_buf (const void *user_buf_, void *kernel_buf_, size_t length) {
  /* Verify that the buffer is within user space */
  if (!is_user_vaddr(user_buf_ + length)) {
    return false;
  }
  uint8_t *kernel_buf = (uint8_t *) kernel_buf_;
  const uint8_t *user_buf = (const uint8_t *) user_buf_;
  for (size_t i = 0; i < length; i++) {
    int byte = get_user(user_buf);
    if (byte == CONTROLLED_PAGE_FAULT) {
      return false;
    }
    *kernel_buf = (uint8_t) byte;
    user_buf++;
    kernel_buf++;
  }
  return true;
}

/* Read a 32 bit word from address uaddr into result
   return false for invalid user memory */
static bool
get_user_word (const void *uaddr, uint32_t *result) {
  return copy_from_user_buf(uaddr, (void *) result, WORD_BYTES);
}

/* Parse a 32 bit argument stored on the user stack at address *uaddr,
   if invalid memory is accessed, the process is terminated with PROC_ERR.
   must be used cautiously, because any resources such as locks / memory
   will not be freed
   otherwise, increment the stack pointer by 4 bytes and return the argument */
static uint32_t
parse_argument (void **uaddr) {
  uint32_t result;
  if (!get_user_word(*uaddr, &result)) {
    process_exit(PROC_ERR);
  }
  *uaddr += WORD_BYTES;
  return result;
}

/* Verify if string points to a valid string in user memory */
static bool
check_valid_string (const char *string) {
  const char *p = string;
  while (is_user_vaddr(p)) {
    int byte = get_user((const uint8_t *) p);
    if (byte == CONTROLLED_PAGE_FAULT) {
      return false;
    }
    if ((char) byte == '\0') {
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
handle_halt(void *esp UNUSED, uint32_t *eax UNUSED) {
  halt();
}


static void NO_RETURN
exit (int status) {
  process_exit (status);
}

static void 
handle_exit (void *esp, uint32_t *eax UNUSED) {
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
handle_exec (void *esp, uint32_t *eax UNUSED) {
  char *cmd_line = (char *) parse_argument(&esp);
  if (!check_valid_string(cmd_line)) {
    process_exit(PROC_ERR);
  }
  pid_t res = exec(cmd_line);
  *eax = res;
}


static int 
wait (pid_t wait_pid) {
  return process_wait((tid_t) wait_pid);
}

static void
handle_wait (void *esp, uint32_t *eax) {
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
handle_create (void *esp, uint32_t *eax) {
  char *file = (char *) parse_argument(&esp);
  unsigned initial_size = (unsigned) parse_argument(&esp);
  if (!check_valid_string(file)) {
    process_exit(PROC_ERR);
  }
  *eax = create(file, initial_size);
}

/* Deletes the file called file. Returns true if successful, false otherwise.*/
static bool 
remove (const char *file) {
  return filesys_remove(file);
}

static void
handle_remove (void *esp, uint32_t *eax) {
  char *file = (char *) parse_argument(&esp);
  *eax = remove(file);
}

#define INVALID_FILE_ERROR -1

/* Opens the file called file. Returns non-negative integer handle called
   file descriptor (fd) or -1 if file could not be opened. A fd of 1 or
   0 is reserved for the console. */
static int 
open (const char *file_name) {
  struct file* file = filesys_open(file_name);
  if (file == NULL)
    return INVALID_FILE_ERROR;
  return add_file (&thread_current()->process->fd_table, file);
}

static void
handle_open (void *esp, uint32_t *eax) {
  char *file = (char *) parse_argument(&esp);
  if (!check_valid_string(file))
    process_exit(PROC_ERR);
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
handle_filesize (void *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  *eax = filesize(fd);
}


/* Helper function for read sycall
   reads length bytes from STDIN into buffer */
static int
read_from_stdin (void *buffer, unsigned length) {
  for (size_t i = 0; i < length; i++) {
    if (!put_user(buffer++, input_getc())) {
      process_exit(PROC_ERR);
    }
  }
  return length;
}

/* Helper function for read syscall
   reads length bytes from the file */
static int
read_from_file (struct file *file, void *buffer, unsigned length) {
  off_t total_bytes_read = 0;
  /* Break up user buffer into smaller chunks of KERNEL_BUF_SIZE */
  while (length > 0) {
    unsigned buf_length = min(length, KERNEL_BUF_SIZE);
    uint8_t kernel_buf[KERNEL_BUF_SIZE];
    /* Safely copy data to user buffer */
    off_t bytes_read = file_read(file, kernel_buf, buf_length);
    if (!copy_to_user_buf(kernel_buf, buffer, bytes_read)) {
      /* No resources allocated, safe to exit */
      process_exit(PROC_ERR);
    }
    length -= buf_length;
    buffer += bytes_read;
    total_bytes_read += bytes_read;
  }
  return total_bytes_read;
}

/* Reads size bytes from the file open as fd into buffer. Returns
   number of bytes actually read. Returns -1 if there is an error
   in getting open file.*/
static int
read (int fd, void *buffer, unsigned length) {
  if (fd == STDOUT_FILENO) {
    return INVALID_FILE_ERROR;
  }
  if (fd == STDIN_FILENO) {
    return read_from_stdin (buffer, length);
  }
  struct fd_table *fd_table = &thread_current()->process->fd_table;
  struct file* file = get_file (fd_table, fd);
  if (file == NULL) {
    return INVALID_FILE_ERROR;
  }
  return read_from_file(file, buffer, length);
}

static void
handle_read (void *esp, uint32_t *eax) {
  int fd = (int) parse_argument(&esp);
  void* buffer = (void*) parse_argument(&esp);
  unsigned length = (unsigned) parse_argument(&esp);
  *eax = read(fd, buffer, length);
}

/* Helper function for write syscall
   writes length bytes to STDOUT */
static int
write_to_stdout (const void *buffer, unsigned length) {
  unsigned length_copy = length;
  while (length > 0) {
    unsigned buf_length = min(length, KERNEL_BUF_SIZE);
    char kernel_buf[KERNEL_BUF_SIZE];
    /* Safely copy data from user buffer */
    if (!copy_from_user_buf(buffer, kernel_buf, buf_length)) {
      /* No resources allocated, safe to exit */
      process_exit(PROC_ERR);
    }
    putbuf(kernel_buf, buf_length);
    length -= buf_length;
    buffer += buf_length;
  }
  return length_copy;
}

static int
write_to_file (struct file *file, const void *buffer, unsigned length) {
  off_t total_bytes_written = 0;
  while (length > 0) {
    unsigned buf_length = min(length, KERNEL_BUF_SIZE);
    char kernel_buf[KERNEL_BUF_SIZE];
    /* Safely copy data from user buffer */
    if (!copy_from_user_buf(buffer, kernel_buf, buf_length)) {
      /* No resources allocated, safe to exit */
      process_exit(PROC_ERR);
    }
    off_t bytes_written = file_write(file, kernel_buf, buf_length);
    total_bytes_written += bytes_written;
    length -= buf_length;
    buffer += bytes_written;

  }
  return total_bytes_written;
}

/* Writes length bytes from buffer to open file fd. 
   Returns number of bytes actually written */
static int 
write (int fd, const void *buffer, unsigned length) {
  if (fd == STDIN_FILENO) {
    return INVALID_FILE_ERROR;
  }
  if (fd == STDOUT_FILENO) {
    return write_to_stdout (buffer, length);
  }
  struct file* file = get_file (&thread_current()->process->fd_table, fd);
  if (file == NULL) {
    return INVALID_FILE_ERROR;
  }
  return write_to_file (file, buffer, length);
}

static void
handle_write (void *esp, uint32_t *eax) {
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
handle_seek (void *esp, uint32_t *eax UNUSED) {
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
handle_tell (void *esp, uint32_t *eax) {
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
handle_close (void *esp, uint32_t *eax UNUSED) {
  int fd = (int) parse_argument(&esp);
  close(fd);
}

/* Maps the file open as fd into the process’s virtual address space. 
   The entire file is mapped into consecutive virtual pages starting at addr.
*/
static mapid_t mmap (int fd, void *addr) {
  mapid_t map_id = MAP_FAILED;
  /* We cant memory map the stdin/stdout or negative fd */
  if (fd < USER_FIRST_FD) {
    return MAP_FAILED;
  }
  int length = filesize(fd);
  if (length == 0 || 
      (uintptr_t)addr % WORD_BYTES != 0 || 
      (uintptr_t)addr == 0) 
  {
    return MAP_FAILED;
  }

  struct fd_table *fd_table = &thread_current()->process->fd_table;
  struct hash *spt = &thread_current()->process->spt;
  struct file* file = get_file (fd_table, fd);
  if (file == NULL) {
      return MAP_FAILED;
  }

  uint32_t *pagedir = thread_current()->process->pagedir;

  int ofs = 0;
  while (length > 0) {
    int read_bytes = min(length, PGSIZE);
    int zero_bytes = PGSIZE - read_bytes;
  
    /* If the page is already being used, we clean up the mapping and return 
       failure. We check the SPT and page table for this. */
    if (
      pagedir_get_page(pagedir, addr) != NULL || 
      stp_lookup(addr, spt) != NULL
    ) {
      munmap(map_id);
      return MAP_FAILED;
    }
    /*We lazy load the page, if valid.*/
    record_file_page(file, ofs, addr, read_bytes, zero_bytes, true);
    ofs += read_bytes;
    length -= read_bytes;
    addr += read_bytes;
  }
}

static void 
handle_mmap (void* esp, uint32_t *eax UNUSED) {
  int fd = (int) parse_argument(&esp);
  void *addr = (void*) parse_argument(&esp);
  *eax = mmap(fd, addr);
}

/* Unmaps the mapping designated by mapping, which must be a mapping ID 
   returned by a previous call to mmap by the same process that has not 
   yet been unmapped.*/
static void munmap (mapid_t mapping) {
  struct mmap_table* mmap_table = thread_current()->process->mmap_table;
  if (
    get_entry(mmap_table, mapping) != NULL &&
    mapping >= FIRST_MAP_ID
  ) {
    free_entry(mmap_table, mapping);
  }
}

static void 
handle_munmap (void* esp, uint32_t *eax UNUSED) {
  mapid_t mapping = (mapid_t) parse_argument (&esp);
  munmap(mapping);
}

#define TOTAL_SYSCALLS 15

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
  [SYS_MMAP]=&handle_mmap,
  [SYS_MUNMAP]=&handle_munmap,
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp_cpy = f->esp;
  uint32_t syscall_num = (uint32_t) parse_argument(&esp_cpy);
  if (syscall_num >= TOTAL_SYSCALLS) {
    process_exit(PROC_ERR);
  }
  handlers[syscall_num](esp_cpy, &f->eax);
}

