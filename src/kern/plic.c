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

#define PLIC_PENDING_OFFSET 0x001000
#define PLIC_ENABLE_OFFSET  0x002000
#define PLIC_CLAIM0_OFFSET  0x200000

#define PLIC_SRCCNT 0x400
#define PLIC_CTXCNT 1

#define PLIC_SRC_PRIO_SIZE 4
#define PLIC_BYTES_PER_WORD 4
#define PLIC_CLAIM_FROM_THRESH_OFFSET 4
#define PLIC_BITS_PER_WORD 32
#define PLIC_ADDR_PER_ENABLE 0x000080
#define PLIC_THRESH_CLAIM_SIZE 0x001000

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
    // context 0 (M mode on hart 0).

    for (i = 0; i < PLIC_SRCCNT; i++) {
        plic_set_source_priority(i, 0);
        plic_enable_source_for_context(0, i);
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
    // Hardwired context 0 (M mode on hart 0)
    trace("%s()", __func__);
    return plic_claim_context_interrupt(0);
}

extern void plic_close_irq(int irqno) {
    // Hardwired context 0 (M mode on hart 0)
    trace("%s(irqno=%d)", __func__, irqno);
    plic_complete_context_interrupt(0, irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * plic_set_source_priority - Sets the priority of the given interrupt source
 * in the PLIC's priority array (addr x0000-x0FFC) to the given level.
 *
 * @srcno: The interrupt source number to set
 * @level: The priority level to write to the priority array
 */
void plic_set_source_priority(uint32_t srcno, uint32_t level) {
    // Check to ensure the source is valid
    if(srcno > 0 && srcno < PLIC_SRCCNT){
        // Calculates the starting address for the context as base + the offset for the given source id.
        uint32_t* src_prio_addr = (PLIC_IOBASE + (PLIC_SRC_PRIO_SIZE * srcno));
        (*src_prio_addr) = level;
    }
        
}

/**
 * plic_source_pending - Checks the bit in the PLIC's pending array (addr x1000-x107C)
 * corresponding to whether there is an interrupt pending for the given source.
 *
 * @srcno: The interrupt source number to check
 *
 * @return: 1 if the source is pending, 0 otherwise or if source out of bounds
 */
int plic_source_pending(uint32_t srcno) {
    // Check to ensure the source is valid, else return 0
    if(srcno >= 0 && srcno < PLIC_SRCCNT){
        // Calculates the starting address for the context as base + x001000
        // Then, divides the id by 32 to get the word number, before multiplying by 8 to get the address offset for the given source id.
        uint32_t* src_pend_addr = PLIC_IOBASE + PLIC_PENDING_OFFSET + (srcno / PLIC_BITS_PER_WORD) * PLIC_BYTES_PER_WORD;
        uint32_t pending_bits = *src_pend_addr;

        // Moves the bit corresponding to the source's status to the least significant position so it can be &-ed to 1 or 0.
        int pending_shifted = pending_bits >> (srcno % PLIC_BITS_PER_WORD);
        return pending_shifted & 1;
    }
    return 0;
}

/**
 * plic_enable_source_for_context - Enables interrupts on the given context for the
 * given source by setting the corresponding bit in the PLIC's enabled array (addr x002000-x1F1F80)
 *
 * @cxtno: The context on which to enable the source
 * @srcno: The interrupt source number to set
 */
void plic_enable_source_for_context(uint32_t ctxno, uint32_t srcno) {
    // Check to ensure the source is valid
    if(srcno >= 0 && srcno < PLIC_SRCCNT){
        // Calculates the starting address for the context as base + x002000 + the offset for the given context
        // Then, divides the id by 32 to get the word number, before multiplying by 8 to get the address offset for the given source id.
        uint32_t* src_enable_addr = PLIC_IOBASE + PLIC_ENABLE_OFFSET + (ctxno * PLIC_ADDR_PER_ENABLE) + (srcno / PLIC_BITS_PER_WORD) * PLIC_BYTES_PER_WORD;

        // Creates a positive bitmask to use with | to set the correct bit for the source.
        uint32_t mask = 1 << (srcno % PLIC_BITS_PER_WORD);
        *src_enable_addr |= mask;
    }
}

/**
 * plic_disable_source_for_context - Disables interrupts on the given context for the
 * given source by clearing the corresponding bit in the PLIC's enabled array (addr x002000-x1F1F80)
 *
 * @cxtno: The context on which to disable the source
 * @srcno: The interrupt source number to clear
 */
void plic_disable_source_for_context(uint32_t ctxno, uint32_t srcid) {
    // Check to ensure the source is valid
    if(srcid >= 0 && srcid < PLIC_SRCCNT){
        // Calculates the starting address for the context as base + x002000 + the offset for the given context
        // Then, divides the id by 32 to get the word number, before multiplying by 8 to get the address offset for the given source id.
        uint32_t* src_disable_addr = PLIC_IOBASE + PLIC_ENABLE_OFFSET + (ctxno * PLIC_ADDR_PER_ENABLE) + (srcid / PLIC_BITS_PER_WORD) * PLIC_BYTES_PER_WORD;

        // Creates a negative bitmask to use with & to clear the correct bit for the source.
        uint32_t mask = ~(1 << (srcid % PLIC_BITS_PER_WORD));
        *src_disable_addr &= mask;
    }
}

/**
 * plic_set_context_threshold - Sets the priority threshold for a given context in the PLIC's thresholds array
 * (addr x200000-x3FFF000). The context will only respond to an interrupt if the priority is above the threshold.
 *
 * @cxtno: The context on which to set the threshold
 * @level: The threshold for priority level
 */
void plic_set_context_threshold(uint32_t ctxno, uint32_t level) {
    // Calculates the address of the register to write to as base + x200000 + the offset for the given context
    uint32_t* threshold_addr = PLIC_IOBASE + PLIC_CLAIM0_OFFSET + (PLIC_THRESH_CLAIM_SIZE * ctxno);
    *threshold_addr = level;
}

/**
 * plic_claim_context_interrupt - Claims an interrupt for the given context by reading from the
 * corresponding claim/complete register (addr x200004-x3FFF004) and returning the interrupt ID.
 *
 * @cxtno: The context for which to claim an interrupt

 * @return: The ID of the highest priority pending interrupt, recieved from the claim/complete register
 */
uint32_t plic_claim_context_interrupt(uint32_t ctxno) {
    // Calculates the address of the register to read from as base + x200004 + the offset for the given context
    uint32_t* claim_addr = PLIC_IOBASE + PLIC_CLAIM0_OFFSET + (PLIC_THRESH_CLAIM_SIZE * ctxno) + PLIC_CLAIM_FROM_THRESH_OFFSET;
    return *claim_addr;
}

/**
 * plic_complete_context_interrupt - Signals that a context has finished responding to an interrupt by
 * writing the interrupt ID back to the corresponding claim/complete register (addr x200000-x3FFF000).
 *
 * @cxtno: The context on which to complete an interrupt
 * @srcno: The source ID of the interrupt that was completed
 */
void plic_complete_context_interrupt(uint32_t ctxno, uint32_t srcno) {
    // Calculates the address of the register to write to as base + x200004 + the offset for the given context
    uint32_t* complete_addr = PLIC_IOBASE + PLIC_CLAIM0_OFFSET + (PLIC_THRESH_CLAIM_SIZE * ctxno) + PLIC_CLAIM_FROM_THRESH_OFFSET;
    *complete_addr = srcno;
}