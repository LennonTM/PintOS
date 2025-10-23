#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "list.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"

#include "fixed-point.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

#define max(x, y) ((x) > (y) ? (x) : (y))

/* Is an array containing 64 lists of threads for each priority. Each
   thread stored is in THREAD_READY state, that is, threads
   that are ready to run but not actually running. Index 63 for PRI_MAX etc  */
static struct list ready_list[PRI_NUM];

/* Bit i is set if and only if ready_list[i] has at least one thread
   allows to determine next thread to run efficiently */
static uint64_t ready_list_mask;

static size_t ready_list_size;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-mlfqs". */
bool thread_mlfqs;

/* Average number of threads ready to run over the past minute. */
fixed_point load_avg;
/* How many ticks it takes for a thread to recalculate priority. */
#define PRIORITY_FREQ 4
/* A list of threads which should have priorities updated 
   on the next PRIORITY_FREQ tick */
struct list threads_to_update;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void thread_init (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *);
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
#ifdef USERPROG
static bool is_user_mode (struct thread *);
#endif

/* Calculates the system-wide load_avg.
   Formula: load_avg = (59/60)*load_avg + (1/60)*ready_threads */
static void
calculate_load_avg (void)
{
  /* ready_threads: running thread + ready_list size. Excludes idle thread. */
  int32_t ready_threads = (int32_t) threads_ready() + 
    (thread_current() != idle_thread ? 1 : 0);

  /* (59/60) * load_avg */
  fixed_point fraction_59_60 = int_to_fixed(59) / 60;
  fixed_point term1 = mulf(fraction_59_60, load_avg);

  /* (1/60) * ready_threads */
  fixed_point term2 = int_to_fixed(ready_threads) / 60;

  load_avg = term1 + term2;
}

/* Calculates the recent_cpu of thread t 
   Formula: recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice */
static void
calculate_recent_cpu (struct thread *t, void *aux UNUSED)
{
  /* Timer interrupt calculates recent_cpu and priority. */
  ASSERT (intr_context());
  /* We don't need to calculate priority and recent_cpu for idle thread. */
  if (t == idle_thread)
    return;

  /* Coefficient C = (2*load_avg)/(2*load_avg + 1) */
  fixed_point double_load_avg = 2 * load_avg;
  fixed_point denominator = addf(double_load_avg, 1);
  fixed_point coefficient = divf(double_load_avg, denominator);

  /* We calculate coefficient first to avoid overflow. */
  /* recent_cpu = C * recent_cpu + nice */
  t->recent_cpu = addf(mulf(coefficient, t->recent_cpu), t->nice);
}

/* Calculates the priority of a thread t based on recent_cpu and nice.
   Formula: priority = PRI_MAX - (recent_cpu / 4) - (nice * 2) */
static void
calculate_priority (struct thread *t, void *aux UNUSED)
{
  /* Timer interrupt calculates recent_cpu/priority. */
  ASSERT (intr_context());
  /* We don't need to calculate priority/recent_cpu for idle thread. */
  if (t == idle_thread)
    return;

  /* Term 1: recent_cpu / 4 */
  fixed_point term1 = t->recent_cpu / 4;

  /* Term 2: nice * 2 */
  int32_t term2 = t->nice * 2;

  /* PRI_MAX - Term 1 - Term 2 */
  fixed_point new_priority_fp = 
    int_to_fixed(PRI_MAX) - term1 - int_to_fixed(term2);
  int32_t new_priority = fixed_to_int_nearest(new_priority_fp);

  /* Priority is clamped to [PRI_MIN, PRI_MAX]*/
  if (new_priority < PRI_MIN)
    new_priority = PRI_MIN;
  else if (new_priority > PRI_MAX)
    new_priority = PRI_MAX;

  ASSERT((PRI_MIN <= new_priority) && (new_priority <= PRI_MAX));

  t->priority = new_priority;
  /* Updates the effective priority and alters the priority-based lists
     accordingly. */
  update_thread_priority(t);
}

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
threading_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&all_list);
  list_init (&threads_to_update);

  /* Initialize lists for all available priorities */
  for (int i = 0; i < PRI_NUM; i++) {
    list_init(&ready_list[i]);
  }
  ready_list_size = 0;
  ready_list_mask = 0;
  load_avg = 0;

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();

  thread_init (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
threading_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Returns the number of threads currently in the ready list. */
size_t
threads_ready (void)
{
  return ready_list_size;
}

static void
add_to_update_list(struct thread * t) {
  if (t->should_update == false) {
    t->should_update = true;
    list_push_front(&threads_to_update, &t->updelem);
  }
}

/* Runs every tick for mlfqs scheduler
   - every second recalculates system-wide load_avg,
     recalculates recent_cpu of each thread
     recalculates priority of each thread
   - every tick increments recent_cpu of the running thread
   - every PRIORITY_FREQ tick, updates priority of all threads
     that are pending an update, which is either a thread
     that got recent_cpu incremented or nice value changed */
static void
update_mlfqs_priorities(void)
{
  ASSERT (thread_mlfqs);
  struct thread *t = thread_current ();

  /* Every second, load_avg, recent_cpu and priority of all threads
     is updated in the order specified */
  if (timer_ticks() % TIMER_FREQ == 0) {
    calculate_load_avg();
    thread_foreach(&calculate_recent_cpu, NULL);
    thread_foreach(&calculate_priority, NULL);
    /* Thread priorities could have changed, so we should yield if the current
       thread does not have highest priority.*/
    yield_if_lower_priority();
    return;
  }

  /* Every tick, recent_cpu of a running thread is incremented
     and the thread is added to the update list, indicating that
     it's pending a priority update */
  if (t != idle_thread) {
    t->recent_cpu = addf (t->recent_cpu, 1);
    add_to_update_list(t);
  }
  /* Every PRIORITY_FREQ tick, except for a per second calculation,
     all threads that are pending an update, get their priority recalculated */
  if (timer_ticks() % PRIORITY_FREQ == 0) {
    /* We iterate through the list removing as we traverse. */
    for (
      struct list_elem *e = list_begin (&threads_to_update); 
      e != list_end (&threads_to_update); 
      e = list_remove (e))
    {
      struct thread *t = list_entry (e, struct thread, updelem);
      /* We calculate the priority of each thread pending an update */
      calculate_priority (t, NULL);
      /* t has already been updated, dont update anymore */
      t->should_update = false;
    }
    /* Thread priorities could have changed, so we should yield if the current
       thread does not have highest priority.*/
    yield_if_lower_priority();
  }
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();
  
  if (thread_mlfqs)
    update_mlfqs_priorities();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (is_user_mode (t))
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  thread_init (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);

  /* New thread might be of higher priority */
  yield_if_lower_priority();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Gets the highest priority which has a non-empty queue in ready list */
static int
get_highest_priority(void){
  int leading_zeros = __builtin_clzll(ready_list_mask);
  /* Highest priority is given by the position
     of the most significant set bit of ready_list_mask */
  return PRI_MAX - leading_zeros;
}

/* Checks if there is a ready thread of higher priority
   than currently running thread
   - immediately preempts the thread if not in the interrupt context
   - if in the interrupt context, calls intr_yield_on_return */
void
yield_if_lower_priority(void) {
  enum intr_level old_level = intr_disable();
  /* Return if there is no other ready thread */
  if (threads_ready() == 0) {
    intr_set_level(old_level);
    return;
  }
  if (get_highest_priority() > thread_current()->effective_priority) {
    if (intr_context()) {
      intr_yield_on_return();
    }
    else {
      thread_yield();
    }
  }
  intr_set_level(old_level);
}

/* Adds a thread to the ready list, maintaining priority order */
static void
add_to_ready_list(struct thread *t) {
  ASSERT(intr_get_level() == INTR_OFF);
  int64_t priority = t->effective_priority;
  ASSERT((PRI_MIN <= priority) && (priority <= PRI_MAX));
  /* Set the priority bit of ready_list_mask to
     indicate that ready_list[priority] is not empty */
  ready_list_mask |= 1ULL << priority;
  /* Put the thread into the back of its corresponding list */
  list_push_back(&ready_list[priority], &t->elem);
  ready_list_size++;
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  add_to_ready_list(t);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  if (thread_current()->should_update)
    list_remove (&thread_current()->updelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) {
    add_to_ready_list(cur);
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's base priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  struct thread *curr_thread = thread_current ();
  curr_thread->priority = new_priority;

  /* Disable interrupts to synchronise updates
     of thread's effective_priority with potential
     priority donations from other threads */
  enum intr_level old_level = intr_disable();
  update_thread_priority(thread_current());
  intr_set_level(old_level);

  yield_if_lower_priority();
}

/* Updates lock's priority based on its waiters
   propagates the change to the lock holder.
   Must be called when a change has been made to the list of waiters

   is mutually recursive with update_thread_priority

   must NOT be called when using mlfq scheduler */
void
update_lock_priority(struct lock* lock) {
  ASSERT (intr_get_level() == INTR_OFF);
  ASSERT (lock != NULL);
  ASSERT (!thread_mlfqs);

  /* Find the lock's highest priority waiter */
  int old_priority = lock->priority;
  lock->priority = PRI_MIN;
  if (!list_empty(&lock->waiters)) {
    /* lock priority is the maximum of waiters priorities */
    lock->priority = list_entry(list_front(&lock->waiters),
                                struct thread,
                                elem)->effective_priority;
  }
  if (lock->priority != old_priority && lock->holder != NULL) {
    /* Lock priority has changed, propagate this */
    /* Remove and reinsert to maintain priority order */
    list_remove(&lock->elem);
    list_insert_ordered(&lock->holder->locks,
                        &lock->elem,
                        sort_locks_by_priority,
                        NULL);
    /* Propagate the change to the lock holder */
    update_thread_priority(lock->holder);
  }
}

/* Removes the thread from ready_list at the queue of specific index */
static void
ready_list_remove(struct thread * t, int index){
  ASSERT ((PRI_MIN <= index) && (index <= PRI_MAX));
  ASSERT (ready_list_mask & (1ULL << index));
  list_remove (&t->elem);
  /* If queue becomes empty remove corresponding bit in the ready list mask */
  if (list_empty(&ready_list[index])) {
    ready_list_mask &= ~(1ULL << index);
  }
  ready_list_size--;
}

/* Updates thread's priority,
   Adjusts thread's position on the priority list
   the thread is on (ready_list or sema, lock, monitor waitlist)

   Call this when a change has been made to
    - a thread's base priority 
    - list of held locks

   is mutually recursive with update_lock_priority */
void
update_thread_priority(struct thread* thread) {
  ASSERT (intr_get_level() == INTR_OFF);
  ASSERT (thread != NULL);

  /* Track old_priority for detecting any changes */
  int old_priority = thread->effective_priority;

  /* effective_priority =
             max(priority, held locks) */
  thread->effective_priority = thread->priority;
  if (!thread_mlfqs && !list_empty(&thread->locks)) {
    int locks_priority = list_entry(list_front(&thread->locks),
                                    struct lock,
                                    elem)->priority;
    if (locks_priority > thread->priority) {
      thread->effective_priority = locks_priority;
    }
  }

  /* If priority did not change, no lists need to be updated */
  if (old_priority == thread->effective_priority)
    return;
  /* Running thread does not take part in read_list or any waitlist */
  if (thread->status == THREAD_RUNNING)
    return;

  /* Adjust thread's position on its waitlist
     to maintain it in decreasing order of priority */
  if (thread->status == THREAD_BLOCKED) {
    ASSERT (thread->waitlist != NULL);
    list_remove(&thread->elem);
    list_insert_ordered(thread->waitlist,
                        &thread->elem,
                        sort_threads_by_effective_priority,
                        NULL);
  }

  if (thread->status == THREAD_READY) {
    ready_list_remove(thread, old_priority);
    add_to_ready_list(thread);
  }
  /* If the thread is blocked by a lock, propagate the change to the lock */
  if (!thread_mlfqs && thread->blocking_lock != NULL) {
    update_lock_priority(thread->blocking_lock);
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->effective_priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  struct thread * t = thread_current ();
  t->nice = nice;
  /* A change in thread niceness can change priority, hence the update. */
  enum intr_level old_level = intr_disable();
  add_to_update_list (t);
  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current ()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return fixed_to_int_nearest(load_avg*100);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return fixed_to_int_nearest(thread_current ()->recent_cpu*100);
}

/* list_less_func that is used to insert threads
 * into lists in the order of decreasing priority */
bool sort_threads_by_effective_priority (const struct list_elem *a_,
                                         const struct list_elem *b_,
                                         void *aux UNUSED) {
  struct thread *a = list_entry(a_, struct thread, elem);
  struct thread *b = list_entry(b_, struct thread, elem);
  /* priorities are compared using (>)
   * to insert a new thread in the end of a cluster
   * of threads with the same priority */
  return a->effective_priority > b->effective_priority;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

#ifdef USERPROG
/* Returns true if T appears to be running a valid user process. */
static bool
is_user_mode (struct thread *t)
{
  return t->process && t->process->pagedir;
}
#endif

/* Does basic initialisation of T as a blocked thread named NAME. */
static void
thread_init (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->should_update = false;
  if(t == initial_thread) {
    /* Niceness and recent_cpu is 0 if no parent thread is present */
    t->nice = 0;
    t->recent_cpu = 0;
  }
  else { 
    /* Niceness and recent_cpu is inherited from its parent if it has one. */
    struct thread * parent = thread_current ();
    ASSERT(parent != NULL);
    t->nice = parent->nice;
    t->recent_cpu = parent->recent_cpu;
  }
    
  t->effective_priority = priority;
#ifdef USERPROG
  t->process = NULL;
#endif   
  t->magic = THREAD_MAGIC;

  t->blocking_lock = NULL;
  t->waitlist = NULL;
  list_init(&t->locks);

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Pops first thread from the highest priority non-empty queue in ready list */
static struct thread *
ready_list_pop(void) {
  ASSERT (intr_get_level() == INTR_OFF);
  ASSERT (ready_list_size != 0);
  ASSERT (ready_list_mask != 0);
  int index = get_highest_priority();
  struct thread* t =
    list_entry (list_pop_front (&ready_list[index]), struct thread, elem);
  /* If queue becomes empty remove corresponding bit in the ready list mask */
  if (list_empty(&ready_list[index])) {
    ready_list_mask &= ~(1ULL << index);
  }
  ready_list_size--;
  return t;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (threads_ready() == 0)
    return idle_thread;
  else
    return ready_list_pop();
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
