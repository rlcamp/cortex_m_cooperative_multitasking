# Cortex-M cooperative multitasking

This codebase implements a simple parameter-free `yield()` function which implements cooperative multitasking and low-power idle on ARM Cortex-M4 microcontrollers. It should work as-is on Cortex-M3, and can be adapted to work on Cortex-M0+.

This allows multiple concurrent threads of execution (tasks) to each be evaluated every time the chip wakes up from sleep due to an interrupt, without requiring all application code to be rewritten to use new APIs for existing functionality. Each task can largely pretend it is the only thing running on the chip, and that the `yield()` function acts as if it simply contains `__DSB(); __WFE();`.

## Cooperative multitasking

In a preemptively-multitasked environment such as your laptop or an embedded Linux system, a scheduler (the "kernel") will switch between tasks (threads and processes) with concurrent lifetimes according to its own logic regardless of where each task is in its execution. This allows progress to be made in all tasks in a seemingly parallel fashion, as if each were running on its own CPU. There are prices to be paid for this abstraction, mainly that threads which need to share state or resource with other threads must do so in a strictly thread-safe manner, via concurrency primitives such as mutexes. A given task can use a mutex or simply disable the task switching to temporarily "opt out" of other threads doing anything which would conflict with it, but it cannot assume much about the state of other theads even while those other threads are paused.

Conversely, in cooperative multitasking, each task explicitly yields whenever it has to wait for some condition to become true, i.e. using `while (!condition) yield();`. Task switching is therefore "op-in" and happens only at safe points. The running task can assume that all other tasks are waiting their turn at such a safe point, rather than at some random intermediate point. This greatly simplifies the required logic when sharing state and resources between tasks with concurrent lifetimes. The only price to be paid is that all tasks must be well-behaved and not hog the processor for too long in between `yield()` calls, which could prevent other tasks from reacting to their awaited conditions in a timely manner.

### Scheduling

Child tasks are started by the main thread on demand. Once started, the main thread and each child are given equal access to the CPU in a simple round-robin fashion, with the exception that when the main thread calls `yield()`, the processor goes into a low-power state until the next interrupt, prior to actually yielding to the next task. In other words, on each wake, all child tasks and then the main task are each evaluated up to the next time they call `yield()` (or `return`, if ending).

In order to ensure timely response to conditions becoming true, tasks must only call `yield()` in a loop around a condition that will be accompanied by a processor wake. Waiting for a condition not accompanied by a processor wake can delay response to the condition by an extra sleep-wake cycle, where the timing of the sleep-wake cycles is solely determined by conditions being waited upon by other tasks. If no tasks are waiting for interrupt-accompanied conditions, the processor may sleep indefinitely.

## License

ISC license.
