/* campbell, isc license */
#include "cortex_m_cooperative_multitasking.h"
#include <stddef.h>

/* these macros are derived from the armv7-m path in https://github.com/rlcamp/coroutine */

#define BOOTSTRAP_CONTEXT(buf, func) do { \
register void * _buf asm("r0") = buf; /* ensure the compiler places this where it will be the argument to func */ \
register void (* _func)(void *) asm("r1") = func; /* ensure the compiler does not place this in a frame pointer register */ \
asm volatile( \
"add lr, pc, (0f - 1f) | 1\n" /* compute address of end of this block of asm, which will be jumped to when returning to this context */ \
"1: push {r7, lr}\n" /* save the future pc value as well as possible frame pointer (which is not allowed in the clobber list) */ \
"str sp, [%0]\n" /* store the current stack pointer in the context buffer */ \
"mov sp, %0\n" /* set the stack pointer to the top of the space below the context buffer */ \
"bx %1\n" /* jump to the child function */ \
".balign 4\n" \
"0:\n" : "+r"(_buf), "+r"(_func) : : "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "lr", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "cc", "memory"); } while(0)

#define SWAP_CONTEXT(buf) do { \
register void * _buf asm("r0") = buf; \
asm volatile( \
"add lr, pc, (0f - 1f) | 1\n" /* compute address of end of this block of asm, which will be jumped to when returning to this context */ \
"1: push {r7, lr}\n" /* save the future pc value and possible frame pointer */ \
"ldr r6, [%0]\n" /* load the saved stack pointer from the context buffer */ \
"str sp, [%0]\n" /* store the current stack pointer in the context buffer */ \
"mov sp, r6\n" /* restore the previously saved stack pointer */ \
"pop {r7, pc}\n" /* jump to the previously saved pc value */ \
".balign 4\n" \
"0:\n" : "+r"(_buf) : : "r1", "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "lr", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "cc", "memory"); } while(0)

static void * context_of_current_child = NULL;

/* a singly-linked list of active child tasks */
static struct child_context * children_head = NULL;

void yield(void) {
    /* if in a child, yielding back to parent... */
    if (context_of_current_child) {
        void * context = context_of_current_child;
        context_of_current_child = NULL;
        SWAP_CONTEXT(context);
    } else {
        /* when yielding from parent, sleep until the next event (i.e. interrupt) */
        sleep_until_event();

        /* loop over children, yielding to each, and removing any that have finished */
        for (struct child_context * this = children_head, ** pn = &children_head;
             this; pn = &this->next, this = this->next) {
                context_of_current_child = this->context;
                SWAP_CONTEXT(context_of_current_child);

                if (!this->func) *pn = this->next;
            }
    }
}

/* application MAY override this if desired */
__attribute((weak)) void sleep_until_event(void) {
    asm volatile("dsb; wfe" :::);
}

__attribute((noreturn)) static void springboard(void * argv) {
    struct child_context * child = argv;
    /* set this so that when either parent or child call the parameter-free yield, it
     can figure out who is calling it and whether to sleep, context switch, or both */
    context_of_current_child = child->context;

    child->func();

    /* tell parent not to context switch back to here, and yield one last time */
    child->func = NULL;
    yield();

    /* springboards must never return */
    __builtin_unreachable();
}

void child_start(struct child_context * child, void (* func)(void)) {
    child->func = func;
    BOOTSTRAP_CONTEXT(child->context, springboard);

    if (child->func) {
        /* if child returned without ever yielding, do not add to list */
        child->next = children_head;
        children_head = child;
    }
}

int child_is_running(struct child_context * child) {
    return child->func != NULL;
}

void * current_task(void) {
    return context_of_current_child;
}
