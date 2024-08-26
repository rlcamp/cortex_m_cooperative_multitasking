/* simple example of a main task and two child tasks, one of which is periodically started
 and asked to stop by the main thread, and all of which blink LEDs at different rates

 compiling this example depends on samd51_init.c from the samd51_blink repo, but most of
 the code is chip-agnostic (will work on any m3/m4/higher), skip to line 136 */

#include "cortex_m_cooperative_multitasking.h"

/* optional sram reduction, should work on any cortex-m */
static void decouple_handlers_from_program_stack(void) {
    /* call this function before any stack switching to ensure that interrupt handlers get
     their own single dedicated call stack, rather than each child stack needing to have
     enough extra headroom to support running the largest interrupt handler. this can
     significantly reduce the total amount of sram required to dedicate to call stacks on
     an embedded processor. call this the beginning of main(), or setup() in arduino code.
     should work on all cortex-m processors. you still need to make sure task stacks have
     at least 104 extra bytes for register storage (less if no FPU or not using it) */
    static char handler_stack[2048] __attribute((aligned(8)));
    asm volatile("cpsid i\n" /* disable irq */
                 "mrs r0, msp\n" /* assuming we are in thread mode using msp, copy current sp */
                 "msr psp, r0\n" /* and store it in psp */
                 "mrs r0, control\n" /* get value of control register */
                 "mov r1, #2\n" /* must be done in two insns because of thumb restrictions */
                 "orr r0, r1\n" /* set bit 1 of control register, to use psp in thread mode */
                 "msr control, r0\n" /* store modified value in control register */
                 "isb\n" /* memory barrier after switching stacks */
                 "msr msp, %0\n" /* set handler stack pointer to top of handler stack */
                 "cpsie i\n" /* enable irq */
                 : : "r"(handler_stack + sizeof(handler_stack)) : "r0", "r1");
}

/* begin samd51-specific stuff */

#if __has_include(<samd51.h>)
/* newer cmsis-atmel from upstream */
#include <samd51.h>
#else
/* older cmsis-atmel from adafruit */
#include <samd.h>
#endif

static void led0_init(void) {
    /* pad PA23 on samd51 (pin 13 on the adafruit feather m4 express) */
    PORT->Group[0].OUTSET.reg = 1U << 23;
    PORT->Group[0].DIRSET.reg = 1U << 23;
    PORT->Group[0].PINCFG[23].reg = 0;
}

static void led0_on(void) { PORT->Group[0].OUTSET.reg = 1U << 23; }
static void led0_off(void) { PORT->Group[0].OUTCLR.reg = 1U << 23; }

static void led1_init(void) {
    /* pad PA22 on samd51 (pin 12 on the adafruit feather m4 express) */
    PORT->Group[0].OUTSET.reg = 1U << 22;
    PORT->Group[0].DIRSET.reg = 1U << 22;
    PORT->Group[0].PINCFG[22].reg = 0;
}

static void led1_on(void) { PORT->Group[0].OUTSET.reg = 1U << 22; }
static void led1_off(void) { PORT->Group[0].OUTCLR.reg = 1U << 22; }

static void led2_init(void) {
    /* pad PA21 on samd51 (pin 11 on the adafruit feather m4 express) */
    PORT->Group[0].OUTSET.reg = 1U << 21;
    PORT->Group[0].DIRSET.reg = 1U << 21;
    PORT->Group[0].PINCFG[21].reg = 0;
}

static void led2_on(void) { PORT->Group[0].OUTSET.reg = 1U << 21; }
static void led2_off(void) { PORT->Group[0].OUTCLR.reg = 1U << 21; }

static void timer_init(void) {
    /* assume GCLK3 is one or the other 32 kHz reference, and we need to make sure it is
     enabled and allowed to run in stdby */
#ifdef CRYSTALLESS
    OSC32KCTRL->OSCULP32K.bit.EN32K = 1;
#else
    OSC32KCTRL->XOSC32K.bit.EN32K = 1;
    OSC32KCTRL->XOSC32K.bit.RUNSTDBY = 1;
    while (!OSC32KCTRL->STATUS.bit.XOSC32KRDY);
#endif

    /* make sure the APB is enabled for TC3 */
    MCLK->APBBMASK.bit.TC3_ = 1;

    /* use the 32 kHz clock peripheral as the source for TC3 */
    GCLK->PCHCTRL[TC3_GCLK_ID].reg = (GCLK_PCHCTRL_Type) { .bit = {
        .GEN = GCLK_PCHCTRL_GEN_GCLK3_Val,
        .CHEN = 1
    }}.reg;
    while (GCLK->SYNCBUSY.reg);

    /* reset the TC3 peripheral */
    TC3->COUNT16.CTRLA.bit.SWRST = 1;
    while (TC3->COUNT16.SYNCBUSY.bit.SWRST);

    TC3->COUNT16.CTRLA.reg = (TC_CTRLA_Type) { .bit = {
        .MODE = TC_CTRLA_MODE_COUNT16_Val, /* use 16 bit counter mode */
        .PRESCALER = TC_CTRLA_PRESCALER_DIV1_Val, /* no prescaler */
        .RUNSTDBY = 1 /* run in stdby */
    }}.reg;

    /* counter resets after the value in cc[0], i.e. its period is that number plus one */
    TC3->COUNT16.WAVE.reg = (TC_WAVE_Type) { .bit.WAVEGEN = TC_WAVE_WAVEGEN_MFRQ_Val }.reg;
    TC3->COUNT16.CC[0].reg = 2048 - 1; /* fire interrupt every 1/16th of a second */
    while (TC3->COUNT16.SYNCBUSY.bit.COUNT);

    /* fire an interrupt at lowest priority whenever counter equals that value */
    TC3->COUNT16.INTENSET.bit.MC0 = 1;
    NVIC_EnableIRQ(TC3_IRQn);

    /* enable the timer */
    while (TC3->COUNT16.SYNCBUSY.reg);
    TC3->COUNT16.CTRLA.bit.ENABLE = 1;
    while (TC3->COUNT16.SYNCBUSY.bit.ENABLE);
}

/* not a volatile, because we want to be able to make volatile reads to it outside an isr
 while not obligating the isr to make volatile writes */
static unsigned ticks = 0;

void TC3_Handler(void) {
    /* clear flag so that interrupt doesn't re-fire */
    TC3->COUNT16.INTFLAG.reg = (TC_INTFLAG_Type){ .bit.MC0 = 1 }.reg;

    /* if ticks were volatile, the compiler could be obligated to generate suboptimal code
     for this, even though we know no other ISRs touch it and non-ISRs only read it */
    ticks++;

    /* arm an321 page 22 fairly strongly suggests this is necessary prior to exception
     return (or will be on future processors) when the first thing the processor wants
     to do afterward depends on state changed in the handler */
    __DSB();
}

/* end samd51-specific stuff */

static unsigned get_ticks(void) {
    return *(volatile unsigned *)&ticks;
}

void sleep_until_event(void) {
    /* this is called by yield() whenever it wants to sleep the processor */
    __DSB();
    __WFE();
}

/* utility function of the type that is almost always a code smell, but is less dumb in
 this context because it doesn't block other cooperative tasks */
static void delay(unsigned long ticks_to_wait) {
    const unsigned ticks_before_waiting = get_ticks();
    while (get_ticks() - ticks_before_waiting < ticks_to_wait)
        yield();
}

static void child_b_func(void) {
    /* simple example of a child tasks that blinks in a pattern forever */
    led1_init();

    while (1) {
        led1_on();
        delay(1);

        led1_off();
        delay(8);
    }
}

static volatile char child_c_should_be_running = 1;

static void child_c_func(void) {
    /* example of a child task that blinks in a pattern until told to stop */
    led2_init();

    while (child_c_should_be_running) {
        led2_on();
        delay(1);

        led2_off();
        delay(7);
    }
}

int main(void) {
    /* this is OPTIONAL, but worth it if there are 2 or more child stacks */
    decouple_handlers_from_program_stack();

    static struct __attribute((aligned(8))) {
        /* needs to be enough to accommodate the deepest call stack needed by any functions
         called in the child, PLUS any interrupt handlers IF we are not using the msp/psp
         switch to provide interrupt handlers with their own dedicated call stack */
        unsigned char stack[2040];

        struct child_context child;
    } child_b, child_c;
    /* additional child tasks would need additional blocks such as the above */

    led0_init();
    timer_init();

    /* unconditionally start one of the child tasks immediately */
    child_start(&child_b.child, child_b_func);

    const unsigned blink_rate_in_ticks = 5;
    unsigned tick_blink_prev = 0;
    unsigned led0_state = 0;

    /* traditional superloop, which would have to evaluate all program logic with the
     control flow turned inside out if we didn't have cooperative multitasking */
    while (1) {
        /* do a volatile read of the global tick count being incremented in the isr */
        const unsigned ticks_now = get_ticks();

        /* maintain led0 in a traditional superloop manner */
        if (ticks_now - tick_blink_prev >= blink_rate_in_ticks) {
            led0_state = !led0_state;
            if (led0_state)
                led0_on();
            else
                led0_off();

            tick_blink_prev += blink_rate_in_ticks;
        }

        /* periodically start the other child task, or task it to stop */
        child_c_should_be_running = !(ticks_now & 128);

        if (child_c_should_be_running && !child_is_running(&child_c.child))
            child_start(&child_c.child, child_c_func);

        /* go to sleep until next interrupt */
        yield();
    }
}
