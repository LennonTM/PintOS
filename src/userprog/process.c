#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/timer.h"

static bool process_init (struct thread *t);
static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Initialises the main thread as a minimal root OS process.
   Note that when this thread/process terminates, the OS will shutdown. */
void
root_process_init (void)
{
  struct thread *cur = thread_current ();
  bool success = process_init (cur);
  
  if (!success)
    PANIC("Unable to initialise root OS process.");
}

/* Handles the parent side of initialising a child_process_entry */
static void
child_entry_init(struct child_process_entry* entry) {
  /* Parent process needs a pointer to child_process_entry */
  struct process *parent_process = thread_current()->process;
  list_push_front(&parent_process->child_entries, &entry->child_entry);

  /* initialise flags, and synchronisation primitives */
  entry->parent_flag = false;
  entry->child_flag = false;
  entry->loading_succeeded = false;
  sema_init(&entry->sema, 0);
  lock_init(&entry->lock);
}

/* Starts a new thread running a user program loaded from CMD_LINE.
   The new thread may be scheduled (and may even exit) before process_execute() returns.
   Returns the new process's thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *cmd_line) 
{
  void *ptr_and_cmd_cpy;
  char *cmd_line_copy;
  tid_t tid;

  /* Create child process entry to communicate with the parent */
  struct child_process_entry* entry = 
    malloc (sizeof(struct child_process_entry));
  if (entry == NULL)
    return TID_ERROR;
  child_entry_init(entry);

  /* Allocate a page for */
  ptr_and_cmd_cpy = palloc_get_page (0);
  if (ptr_and_cmd_cpy == NULL)
    return TID_ERROR;

  /* Pointer to child process entry */
  void **ptr = (void **)ptr_and_cmd_cpy;
  *ptr = entry;

  /* Make a copy of CMD_LINE.
     Otherwise there's a race between the caller and load(). */
  cmd_line_copy = (char *)ptr_and_cmd_cpy + WORD_BYTES;
  strlcpy (cmd_line_copy, cmd_line, PGSIZE - WORD_BYTES);

  /* Threads share a limit of 16 characters */
  /* Parse thread_name */
  char thread_name[MAX_NAME_LENGTH];
  /* find the first non-whitespace character */
  char *cmd_line_ptr = cmd_line_copy;
  while (*cmd_line_ptr == ' ')
    cmd_line_ptr++;
  /* copy MAX_NAME_LENGTH characters and tokenise it to remove whitespace */
  char *save_ptr;
  strlcpy (thread_name, cmd_line, MAX_NAME_LENGTH);
  strtok_r (thread_name, " ", &save_ptr);

  /* Create a new thread to execute CMD_LINE. */
  tid = thread_create (
    thread_name, 
    PRI_DEFAULT, 
    start_process,
    ptr_and_cmd_cpy);

  /* wait until the process exits, or successfully creates the thread */
  sema_down(&entry->sema);

  /* Pass tid */
  entry->pid = tid;

  /* If process loading failed (which includes TID_ERROR)
     free the auxiliary data */
  if (!entry->loading_succeeded) {
    list_remove(&entry->child_entry);
    free(entry);
    palloc_free_page (ptr_and_cmd_cpy);
    return TID_ERROR;
  }

  return tid;
}

/* Basic initialisation of a process running on thread T. */
static bool 
process_init (struct thread *t)
{
  /* Create process struct to store process meta-data */
  struct process *process = malloc (sizeof(struct process));
  if (process == NULL)
    return false;

  /* Initialise process attributes. */
  process->pagedir = NULL;
  process->thread = t;
  process->recover_flag = false;
  process->fd_table = malloc (sizeof(struct list *));
  list_init(process->fd_table);

  /* Initialise process id */
  list_init(&process->child_entries);
  
  /* Link thread to its process. */
  t->process = process;
  return true;
}

/* writes int to stack and increments the esp
   unlike pushing to the stack, this grows upwards*/
static void write_int_to_stack(void **esp, int n) {
  int *esp_int = (int *)*esp;
  *esp_int = n;
  *esp = (char *)(*esp) + sizeof(n);
}
/* writes string to stack and increments the esp
   unlike pushing to the stack, this grows upwards*/
static void write_string_to_stack(void **esp, char *str) {
  char *esp_str = (char *)*esp;
  int str_len = strlen(str);
  strlcpy(esp_str, str, str_len + 1);
  *esp = (char *)(*esp) + (str_len + 1) * sizeof(char);
}
/* writes pointer to stack and increments the esp
   unlike pushing to the stack, this grows upwards*/
static void write_pointer_to_stack(void **esp, void *ptr) {
  void **esp_ptr = (void **)*esp;
  *esp_ptr = ptr;
  *esp = (char *)(*esp) + sizeof(ptr);
}


/* A thread function that loads a user process and starts it
   running.
   Args contains a pointer to a child_process_entry followed by cmd_line_cpy */
static void
start_process (void *args_)
{
  struct thread *cur = thread_current ();

  /* for loop to calculate the following
   * args_len:    string length (including whitespace)
   * space_count: number of spaces found 
   * argc         number of arguments found */
  int args_len = 0;
  int space_count = 0;
  int argc = 0;
  bool is_arg = false;

  char *args = (char *)args_ + WORD_BYTES;

  /* args_len increases as we traverse the string */
  for (; args[args_len] != '\0'; args_len++) {
    if (args[args_len] == ' ') {
      /* whitespace, then increment space_count */
      space_count++;
      is_arg = false;
    } else {
      if (!is_arg) {
        /* switching from whitespace to argument, increment argc */
        is_arg = true;
        argc++;
      }
    }
  }

  /* begin tokenising args_
   * filename is the first argument */
  char *save_ptr;
  char *token = strtok_r (args, " ", &save_ptr);

  char *file_name = token;
  struct intr_frame if_;
  bool success;
  
  /* Initialise process. */
  success = process_init (cur);
  /* Handle child_process_entry  */
  struct child_process_entry *entry = *(struct child_process_entry **)args_;
  cur->process->entry = entry;

  if (success)
    {
      /* Initialize interrupt frame and load executable. */
      memset (&if_, 0, sizeof if_);
      if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
      if_.cs = SEL_UCSEG;
      if_.eflags = FLAG_IF | FLAG_MBS;
      success = load (file_name, &if_.eip, &if_.esp);
    }

  /* If load failed, terminate the process. */
  if (!success) {
    process_exit (PROC_ERR); // entry->return_value == -1
  }

  /* push arguments onto the stack */
  /* space to allocate to argument strings is equal to 
   * (args_len - space_count) characters and argc null terminals */
  void *esp_str  = if_.esp - (args_len - space_count + argc);

  /* space to allocate to argument pointers array is equal to 
   * (size of a string pointer) * (number of arguments + null terminator) 
   * Before allocating space, memory align the stack pointer */
   /* We save a copy of esp_ptr, so we can point to the first argument later */
  void *esp_str_aligned = (esp_str - ((uint32_t)esp_str % BYTE_SIZE));
  void *esp_ptr = esp_str_aligned - BYTE_SIZE * (argc + 1);
  void *esp_ptr_cpy = esp_ptr;

  /* set new stack pointer */
  if_.esp = esp_ptr - 3 * WORD_BYTES;

  /* Exit if the user stack exceeds one page after arument parsing */
  if (if_.esp < PHYS_BASE - PGSIZE) {
    process_exit(PROC_ERR);
  }

  /* We tokenise the cmd line and write the 
     token and their pointer to the stack */
  for (; token != NULL; token = strtok_r (NULL, " ", &save_ptr)) {
    write_pointer_to_stack(&esp_ptr, esp_str);
    write_string_to_stack (&esp_str, token);
  }
  write_pointer_to_stack(&esp_ptr, NULL);

  /* we write return address, argc and argv to stack*/
  void *esp_cpy = if_.esp;
  write_pointer_to_stack(&esp_cpy, NULL);
  write_int_to_stack(&esp_cpy, argc);
  write_pointer_to_stack(&esp_cpy, esp_ptr_cpy);

  /* After loading, we are done with the command-line copy passed to us by process_execute. */
  palloc_free_page (args_);

  entry->loading_succeeded = true;
  sema_up(&entry->sema);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Will destroy itself if both of the following occur
   1. Parent has exited or parent finished waiting
   2. Child has exited
   Returns true if the child process destroyed */
static void
handle_entry_destruction(
  struct child_process_entry *entry, 
  bool is_parent) 
{
  /* only one process can access this entry's flags at once */
  lock_acquire(&entry->lock);

  /* Set corresponding flag  */
  if (is_parent) {
    entry->parent_flag = true;
  } else {
    entry->child_flag = true;
  }

  /* Check whether we should destroy the entry */
  bool destroy_self = entry->parent_flag && entry->child_flag;
  if (destroy_self) {
    list_remove(&entry->child_entry);
  } else {
    sema_up(&entry->sema);
  }
  lock_release(&entry->lock);

  if (destroy_self) {
    free(entry);
  }
}

/* Waits for thread TID to die and returns its exit status. 
 * If it was terminated by the kernel (i.e. killed due to an exception), 
 * returns -1.  
 * If TID is invalid or if it was not a child of the calling process, or if 
 * process_wait() has already been successfully called for the given TID, 
 * returns -1 immediately, without waiting.
 * 
 * This function will be implemented in task 2.
 * For now, it does nothing. */
int
process_wait (tid_t child_tid ) 
{
  struct process *cur = thread_current()->process;
  struct list_elem *e;
  /* find the correct child */
  for (
      e = list_begin (&cur->child_entries); 
      e != list_end (&cur->child_entries); 
      e = list_next (e)) 
  {
    struct child_process_entry *entry = 
      list_entry (e, struct child_process_entry, child_entry);
    /* pid is defined to be equal to tid */
    if (entry->pid == child_tid) {
      /* child found */
      sema_down(&entry->sema);
      int result = entry->return_value;
      bool is_parent = true;
      handle_entry_destruction(entry, is_parent);
      return result;
    }
  }
  /* child not found */
  return PROC_ERR;
}

/* Free the current process's resources and then exit the underlying thread. */
void
process_exit (int exit_code)
{
  struct process *cur = thread_current ()->process;
  uint32_t *pd;
  
  struct child_process_entry *entry = cur->entry;
  
  /* Handle exec and wait syscalls.
     For itself and all children, set the flag and potentially destroy */
  bool is_parent = true;
  /* Pass exit_code before_hand */
  entry->return_value = exit_code;
  handle_entry_destruction(entry, is_parent);
  
  /* Destory all children */
  is_parent = false;
  struct list_elem *e = list_begin (&cur->child_entries);
  while (e != list_end (&cur->child_entries)) 
  {
    struct child_process_entry *child_entry = 
      list_entry (e, struct child_process_entry, child_entry);
    /* list_elem may be removed, so access list_next beforehand */
    struct list_elem *e_next = list_next (e);
    handle_entry_destruction(child_entry, is_parent);
    e = e_next;
  }
  
  /* Clean up all process memory footprint, if it exists. */
  if (cur != NULL)
    {  

      /* Destroy the current process's page directory and switch back
         to the kernel-only page directory. */
      pd = cur->pagedir;
      if (pd != NULL) 
        {
          /* Correct ordering here is crucial.  We must set
             cur->pagedir to NULL before switching page directories,
             so that a timer interrupt can't switch back to the
             process page directory.  We must activate the base page
             directory before destroying the process's page
             directory, or our active page directory will be one
             that's been freed (and cleared). */
          cur->pagedir = NULL;
          pagedir_activate (NULL);
          pagedir_destroy (pd);
        }
  
      /* Finally, destroy the current process's struct */
      thread_current ()->process = NULL;
      free(cur);
    }
    
  /* Now terminate the thread underlying this process. */
  thread_exit();
  NOT_REACHED ();
}

/* Sets up the CPU for running user code in the current thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct process *cur = thread_current ()->process;
  
  /* Activate thread's page tables if running an active process. */
  if (cur != NULL)  
    pagedir_activate (cur->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct process *cur = thread_current ()->process;
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  cur->pagedir = pagedir_create ();
  if (cur->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
        
      /* Get a new page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL){
        return false;
      }
        
      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
      {
        palloc_free_page (kpage);
        return false; 
      }     

      /* Load data into the page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes){
        return false; 
      }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct process *cur = thread_current ()->process;

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (cur->pagedir, upage) == NULL
          && pagedir_set_page (cur->pagedir, upage, kpage, writable));
}
