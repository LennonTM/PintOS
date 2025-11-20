#include <stddef.h>
#include <stdbool.h>

struct frame_table_entry {
    /* Ensure kernel doesn't page fault accessing */
    bool pinned;

    /* Index in array is the implicit kernel frame and physical address */

    /* Accessed bit for eviction policy */
    bool accessed;
};

void frame_table_init (void);