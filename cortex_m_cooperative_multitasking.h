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

/* caller must provide this function, which is expected to __DSB(); __WFE(); or equivalent */
extern void sleep_until_event(void);

/* returns an opaque identifier which can be used in comparisons */
void * current_task(void);
