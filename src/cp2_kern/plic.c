// plic.c - RISC-V PLIC
//

#include "plic.h"
#include "console.h"

#include <stdint.h>

// COMPILE-TIME CONFIGURATION
//

// *** Note to student: you MUST use PLIC_IOBASE for all address calculations,
// as this will be used for testing!

#ifndef PLIC_IOBASE
#define PLIC_IOBASE 0x0C000000
#endif

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 2

// PLIC MEMORY OFFSETS
#define PLIC_PEND   0x001000
#define PLIC_ENCTX  0x002000
#define PLIC_PRIO   0x200000
#define PLIC_CLAIM  0x200004

#define BLCK_SIZE   0x80    // -> 128 bytes per context block
#define CTX_SIZE    0x1000  // -> 4 kerabytes per context

// INTERNAL FUNCTION DECLARATIONS
//

// *** Note to student: the following MUST be declared extern. Do not change these
// function delcarations!

extern void plic_set_source_priority(uint32_t srcno, uint32_t level);
extern int plic_source_pending(uint32_t srcno);
extern void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno);
extern void plic_set_context_threshold(uint32_t ctxno, uint32_t level);
extern uint32_t plic_claim_context_interrupt(uint32_t ctxno);
extern void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno);

// Currently supports only single-hart operation. The low-level PLIC functions
// already understand contexts, so we only need to modify the high-level
// functions (plic_init, plic_claim, plic_complete).

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
    int i;

    // Disable all sources by setting priority to 0, enable all sources for
    // context 1 (S mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(1, i);
    }
}

extern void plic_enable_irq(int irqno, int prio) {
    trace("%s(irqno=%d,prio=%d)", __func__, irqno, prio);
    plic_set_source_priority(irqno, prio);
}

extern void plic_disable_irq(int irqno) {
    if (0 < irqno)
        plic_set_source_priority(irqno, 0);
    else
        debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_irq(void) {
    // Hardwired context 1 (S mode on hart 0)
    trace("%s()", __func__);
    return plic_claim_context_interrupt(1);
}

extern void plic_close_irq(int irqno) {
    // Hardwired context 1 (S mode on hart 0)
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(1, irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * @name plic_set_source_priority
 * 
 * @arg uint32_t srcno  -> specified source number
 * @arg uint32_t level  -> value that we want to set the specific interrupt source priority to
 * 
 * @return void         -> no return
 * 
 * @purpose:
 *  The purpose of this function is to take a interrupt source number as input and change the priority of that
 *  interrupt source to that of the inputted level.
 * 
 * @side_effects:
 *  No side effects outside of setting a new priority.
 */
void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    if (srcno >= PLIC_SRCCNT || srcno <= 0) return; // Ensure the source number is within the valid bounds
    uintptr_t offset = PLIC_IOBASE + (srcno * 4);    // Calculate appropriate offset, where each source is 4 bytes
    *(volatile uint32_t *)(offset) = level;         // Actually set the value of the source priority
}

/**
 * @name plic_source_pending
 * 
 * @arg uint32_t srcno  -> specified source number
 * 
 * @return int          -> binary '1' or '0', where '1' means an interrupt source is pending and '0' means otherwise.
 * 
 * @purpose:
 *  The purpose of this function is to check if a specific interrupt source is pending.
 * 
 * @side_effects:
 *  No side effects.
 */
int plic_source_pending(uint32_t srcno) {
    if (srcno >= PLIC_SRCCNT || srcno <= 0) return 0;
    uint32_t word_index = srcno / 32;   // Find the 32-bit word where our intended bit is given the source number
    uint32_t bit_index = srcno % 32;    // Find the exact pending bit for the given the source number
    uintptr_t offset = PLIC_IOBASE + PLIC_PEND + (word_index * 4);
    uint32_t result = (*(volatile uint32_t *)(offset) >> bit_index) & 1; // Move the pending bit to the LSB position and check if it's '1'
    return result;                      // Return '1' if pending, and '0' otherwise
}

/**
 * @name plic_enable_source_for_context
 * 
 * @arg uint32_t ctxno  -> specified context number
 * @arg uint32_t srcno  -> specified source number
 * 
 * @return void         -> no return
 * 
 * @purpose:
 *  The purpose of this function is to enable a specific interrupt source within a specific context.
 * 
 * @side_effects:
 *  No side effects outside of the possibility of a context now taking interrupts from a source.
 */
void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    if (ctxno >= PLIC_CTXCNT || srcno >= PLIC_SRCCNT || ctxno < 0 || srcno <= 0) return;
    uint32_t word_index = srcno / 32;
    uint32_t bit_index = srcno % 32;
    uintptr_t offset = PLIC_IOBASE + PLIC_ENCTX + (ctxno * BLCK_SIZE) + (word_index * 4);
    *(volatile uint32_t *)(offset) |= (1 << bit_index); // Enable interrupt source, if not already enabled 
}

/**
 * @name plic_disable_source_for_context
 * 
 * @arg uint32_t ctxno  -> specified context number
 * @arg uint32_t srcno  -> specified source number
 * 
 * @return void         -> no return
 * 
 * @purpose:
 *  The purpose of this function is to disable a specific interrupt source within a specific context
 * 
 * @side_effects:
 *  No side effects outside of the possibility of a context not taking interrupts from a source
 */
void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    if (ctxno >= PLIC_CTXCNT || srcno >= PLIC_SRCCNT || ctxno < 0 || srcno <= 0) return;
    uint32_t word_index = srcno / 32;
    uint32_t bit_index = srcno % 32;
    uintptr_t offset = PLIC_IOBASE + PLIC_ENCTX + (ctxno * BLCK_SIZE) + (word_index * 4);
    *(volatile uint32_t *)(offset) &= ~(1 << bit_index);    // Disable interrupt source, if not already disabled
}

/**
 * @name plic_set_context_threshold
 * 
 * @arg uint32_t ctxno  -> specified context number
 * @arg uint32_t srcno  -> specified source number
 * 
 * @return void         -> no return
 * 
 * @purpose:
 *  The purpose of this function is to set the threshold to a specific interrupt source number 
 *  within a specific context.
 * 
 * @side_effects:
 *  No side effects outside of the possibility that old sources become outside the new threshold, or
 *  that old sources are now within the new threshold.
 */
void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    if (ctxno >= PLIC_CTXCNT || ctxno < 0) return;
    uintptr_t offset = PLIC_IOBASE + PLIC_PRIO + (ctxno * CTX_SIZE);
    *(volatile uint32_t *)(offset) = level;
}

/**
 * @name plic_claim_context_interrupt
 * 
 * @arg uint32_t ctxno  -> specified context number
 * 
 * @return uint32_t     -> ID of the highest priority interrupt that is pending
 * 
 * @purpose:
 *  The purpose of this function is to claim an interrupt for the specified context by reading from
 *  the claim register so that the PLIC knows we are servicing that interrupt.
 * 
 * @side_effects:
 *  No side effects outside of the pending bit getting automatically cleared so that the interrupt
 *  will not be re-triggered.
 */
uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    if (ctxno >= PLIC_CTXCNT || ctxno < 0) return 0;
    uintptr_t offset = PLIC_IOBASE + PLIC_CLAIM + (ctxno * CTX_SIZE);
    return *(volatile uint32_t *)(offset);
}

/**
 * @name plic_complete_context_interrupt
 * 
 * @arg uint32_t ctxno  -> specified context number
 * @arg uint32_t srcno  -> specified source number
 * 
 * @return void         -> no return
 * 
 * @purpose:
 *  The purpose of this function is to mark the completion of an interrupt by writing the interrupt
 *  source number back to the claim register it was taken from.
 * 
 * @side_effects:
 *  No side effects outside of allowing new interrupts to come from the specified source within the
 *  specified context.
 */
void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    if (ctxno >= PLIC_CTXCNT || srcno >= PLIC_SRCCNT || ctxno < 0 || srcno <= 0) return;
    uintptr_t offset = PLIC_IOBASE + PLIC_CLAIM + (ctxno * CTX_SIZE);
    *(volatile uint32_t *)(offset) = srcno;
}