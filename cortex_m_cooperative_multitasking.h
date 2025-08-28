/* Copyright 2021-2025 Richard Campbell

 Permission to use, copy, modify, and/or distribute this software for any purpose with or
 without fee is hereby granted, provided that the above copyright notice and this
 permission notice appear in all copies.

 THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO
 THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT
 SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR
 ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 USE OR PERFORMANCE OF THIS SOFTWARE.
*/

struct child_context {
    /* the context switching macros expect this to immediately follow the stack space */
    unsigned char context[4] __attribute((aligned(8)));

    /* springboard runs this and then sets it to NULL, which the parent can detect */
    void (* volatile func)(void);

    /* so that the parent can loop through these in the parameter-free yield() */
    struct child_context * next;
};

/* any call site in parent or children can loop on calls to this when waiting for some
 condition to be true, as long as that condition is accompanied by an interrupt or other
 event which would wake the processor from WFE. if the latter condition is not true, a
 call site can loop on "while (!condition) { __SEV(); yield(); }" in order to inhibit the
 call to WFE within yield, thereby effectively causing the whole chip to spinloop on all
 waited-for conditions without sleeping. this should be used sparingly due to increased
 power consumption, but allows other threads to continue to make progress in more cases */
void yield(void);

/* parent calls this to start a child */
void child_start(struct child_context * child, void (* func)(void));

/* parent can call this to determine whether an already-started child is still running */
int child_is_running(struct child_context * child);

/* application MAY override this at link time, it is expected to call __DSB(); __WFE(); */
extern void sleep_until_event(void);

/* returns an opaque identifier which can be used in comparisons */
void * current_task(void);
