/* Copyright 2021-2026 Richard Campbell

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
#include "cortex_m_cooperative_multitasking.h"
#include <stddef.h>

/* these macros are derived from the armv7-m path in https://github.com/rlcamp/coroutine */

#define BOOTSTRAP_CONTEXT(bufto, buffrom, func) do { \
register void * _bufto asm("r0") = bufto; /* ensure the compiler places this where it will be the argument to func */ \
register void * _buffrom asm("r4") = buffrom; /* ensure the compiler does not place this in a frame pointer register */ \
register void (* _func)(void *) asm("r5") = func; /* ensure the compiler does not place this in a frame pointer register */ \
asm volatile( \
".balign 4\n" /* pc-relative adds implicitly round down to 4-byte alignment at runtime */ \
"add lr, pc, (0f - 1f) | 1\n" /* compute address of end of this block of asm, which will be jumped to when returning to this context */ \
"1: push {r7, lr}\n" /* save the future pc value as well as possible frame pointer (which is not allowed in the clobber list) */ \
"str sp, [%1]\n" /* store the current stack pointer in the context buffer */ \
"mov sp, %0\n" /* set the stack pointer to the top of the space below the context buffer */ \
"bx %2\n" /* jump to the child function */ \
".balign 4\n" \
"0:\n" : "+r"(_bufto), "+r"(_buffrom), "+r"(_func) : : "r1", "r2", "r3", "r6", "r8", "r9", "r10", "r11", "r12", "lr", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "cc", "memory"); } while(0)

#define SWAP_CONTEXT(bufto, buffrom) do { \
register void * _bufto asm("r4") = bufto; \
register void * _buffrom asm("r5") = buffrom; \
asm volatile( \
".balign 4\n" \
"add lr, pc, (0f - 1f) | 1\n" /* compute address of end of this block of asm, which will be jumped to when returning to this context */ \
"1: push {r7, lr}\n" /* save the future pc value and possible frame pointer */ \
"ldr r6, [%0]\n" /* load the saved stack pointer from the context buffer */ \
"str sp, [%1]\n" /* store the current stack pointer in the context buffer */ \
"mov sp, r6\n" /* restore the previously saved stack pointer */ \
"pop {r7, pc}\n" /* jump to the previously saved pc value */ \
".balign 4\n" \
"0:\n" : "+r"(_bufto), "+r"(_buffrom) : : "r0", "r1", "r2", "r3", "r6", "r8", "r9", "r10", "r11", "r12", "lr", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "cc", "memory"); } while(0)

static struct child_context main_context;

static struct child_context * current_child = &main_context;

/* a singly-linked list of active child tasks */
static struct child_context * children_head = &main_context;

void yield(void) {
    struct child_context * prior = current_child;
    current_child = current_child->next;

    if (!current_child) {
        sleep_until_event();
        current_child = children_head;
    }

    if (prior != current_child)
        SWAP_CONTEXT(current_child->context, prior->context);
}

/* application MAY override this if desired */
__attribute((weak)) void sleep_until_event(void) {
    asm volatile("dsb; wfe" :::);
}

__attribute((noreturn)) static void springboard(void * argv) {
    struct child_context * child = argv;

    child->func();

    /* remove self from singly-linked list of running tasks */
    struct child_context ** prev_next = &children_head;
    for (struct child_context * this = children_head; this && this != child; this = this->next)
        prev_next = &this->next;
    *prev_next = child->next;

    child->func = NULL;

    /* make sure other tasks (not just main) get to react to this task ending */
    asm volatile("sev" :::);

    /* and yield for the final time */
    yield();

    /* springboards must never return */
    __builtin_unreachable();
}

void child_start(struct child_context * child, void (* func)(void)) {
    child->next = children_head;
    children_head = child;

    child->func = func;
    current_child = child;
    BOOTSTRAP_CONTEXT(child->context, &main_context, springboard);
}

int child_is_running(struct child_context * child) {
    return child->func != NULL;
}

const struct child_context * current_task(void) {
    return current_child;
}
