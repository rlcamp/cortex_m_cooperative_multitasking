# Cortex-M cooperative multitasking

This codebase implements a simple parameter-free `yield()` function which enables cooperative multitasking and low-power idle on ARM Cortex-M microcontrollers that implement the `wfe` instruction, such as the M3, M4, and M33.

This allows multiple concurrent threads of execution (tasks) to be evaluated every time the chip wakes up from sleep due to an interrupt, without requiring all application code to be rewritten to use new APIs for existing functionality. Each task can largely pretend it is the only thing running on the chip, and that the `yield()` function acts as if it simply contains `__DSB(); __WFE();`.

## Cooperative multitasking

In a preemptively-multitasked environment such as your laptop or an embedded Linux system, a scheduler (the "kernel") will switch between tasks (threads and processes) with concurrent lifetimes whenever it wants to, regardless of where each task is in its execution. This allows all tasks to make progress in a seemingly parallel fashion, as if each were running on its own CPU. There are prices to be paid for this abstraction, mainly that threads which need to share state or resource with other threads must do so in a strictly thread-safe manner, via concurrency primitives such as mutexes. A given task can use a mutex or simply disable the task switching to temporarily "opt out" of other threads doing anything which would conflict with it, but it cannot assume much about the state of other theads even while those other threads are paused.

Conversely, in cooperative multitasking, each task explicitly yields whenever it has to wait for some condition to become true, i.e. using `while (!condition) yield();`. Task switching is therefore "op-in" and happens only at safe points. The running task can assume that all other tasks are waiting at such a safe point, rather than at some random intermediate point. This greatly simplifies the required logic when sharing state and resources between tasks with concurrent lifetimes. The only price to be paid is that all tasks must be well-behaved and not hog the processor for too long in between `yield()` calls, which could prevent other tasks from reacting to their awaited conditions in a timely manner.

### Scheduling

Child tasks are started by the main task on demand. Once started, the main task and each child are evaluated in a simple round-robin fashion, with the exception that when the main task calls `yield()`, the processor goes into a low-power state until the next interrupt, prior to actually yielding to the next task. In other words, on each wake, all child tasks and then the main task are each evaluated up to the next time they call `yield()` (or `return`, if ending).

In order to ensure timely response to conditions becoming true, tasks must only call `yield()` in a loop around a condition that will be accompanied by a processor wake. Waiting for a condition not accompanied by a processor wake can delay response to the condition by an extra sleep-wake cycle, where the timing of the sleep-wake cycles is solely determined by conditions being waited upon by other tasks. If no tasks are waiting for interrupt-accompanied conditions, the processor may sleep indefinitely.

If a condition needs to be waited upon that is not accompanied by an interrupt when it becomes true, a call site can loop on `while (!condition) { __SEV(); yield(); }` in order to inhibit the single `wfe` within `yield()`, thereby causing the whole chip to spinloop on all waited-for conditions without sleeping, until the offending condition becomes true. This should be used sparingly due to increased power consumption, but allows other tasks to continue to make progress in cases where they would otherwise be blocked indefinitely.

## Why

### Why not use preemptive multitasking

It turns out to be MUCH simpler and more practical to achieve properly low-power idle states while waiting for something to happen, and immediately respond once it does, if each thing doing the waiting can express it as "wake me up whenever anything happens, and I will check whether the thing that happened is what i was waiting for."

Cooperative multitasking allows each piece of code to be written as if it were the only thing running on the microcontroller, with the single change of looping on a `yield()` function whenever one would have looped on `__WFE()` previously.

### Why not use an RTOS

An RTOS exists, in part, to attempt to solve a problem introduced by preemptive multitasking, by moving the logical "wait for X" from application code into OS kernel code. Unfortunately, this requires that all possible things that could have been waited for, and how to wait for them in a power-efficient and low-latency way, need to have been anticipated and implemented by the OS kernel authors.

### Why not do properly nonblocking everything using traditional horrifying inside-out state machines

Because they are extremely brittle and nearly impossible to combine, and when I have implemented data acquisition firmware with this paradigm I ended up saying "no" a lot when people asked for added functionality.

### Why not use protothreads

Protothreads don't have their own call stacks - the resulting code can look superficially similar to proper stackful coroutines, but under the hood they work very differently. Proper stackful coroutines have far fewer restrictions - in particular, protothreads can only `yield` from the top level function in the thread, rather than from within a function called by it (such as `delay()` for example).

### Isn't `delay()` a code smell?

Yes, if it actually blocks other code. It is trivial to implement `delay()` such that it contains a loop on the aforementioned `yield()`, and therefore only blocks the calling task.

## License

ISC license.

## Caveats

As currently implemented, all child tasks must be immediate children of the parent. Therefore, child tasks must not attempt to directly create other child tasks, but must arrange for the parent to create them. While it is possible to hide this detail within the implementation of `child_start()` and `yield()`, the consequence would be a slight increase in the cost of every `yield()` even when this feature is not needed. It is therefore recommended that children arrange for the main task to start other children on its behalf, at the application level.

It should be possible to mix this implementation with the [more strict structured concurrency framework](https://github.com/rlcamp/coroutine) from which this code borrows its context switching primitives, which DOES allow for child coroutines to in turn start their own children, forming arbitrarily deep trees of coroutines. In such a combined usage, each tree of coroutines would appear as a single task from the perspective of the parameter-free yield. This combined usage is rougly analogous to the distinction between processes and threads in a multitasking operating system.

For example: if `main()` starts task A using `child_start()`, and then task A starts coroutine B using `coroutine_create()`, and then coroutine B calls `yield()`, execution will cycle through `main()` and any other tasks started by it, call `wfe` once, and then end up at the next line after `yield()` in B. Execution will not return to A until B calls `yield_to()`. Task A can then either call `yield()` itself, which will transfer execution to either `main()` and its immediate children, or `from()`, which will transfer execution back to B. Since A and B have a structured concurrency requirement, B must `return` before A is allowed to.
