// timer.c
//

#include "timer.h"
#include "thread.h"
#include "csr.h"
#include "intr.h"
#include "halt.h" // for assert

// EXPORTED GLOBAL VARIABLE DEFINITIONS
// 

char timer_initialized = 0;

struct condition tick_1Hz;
struct condition tick_10Hz;

uint64_t tick_1Hz_count = 0;
uint64_t tick_10Hz_count = 0;

#define MTIME_FREQ 10000000 // from QEMU include/hw/intc/riscv_aclint.h

// INTERNVAL GLOBAL VARIABLE DEFINITIONS
//

// INTERNAL FUNCTION DECLARATIONS
//

static inline uint64_t get_mtime(void);
static inline void set_mtime(uint64_t val);
static inline uint64_t get_mtimecmp(void);
static inline void set_mtimecmp(uint64_t val);

// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(void) {
    assert (intr_initialized);
    condition_init(&tick_1Hz, "tick_1Hz");
    condition_init(&tick_10Hz, "tick_10Hz");

    // Set mtimecmp to maximum so timer interrupt does not fire

    set_mtime(0);
    set_mtimecmp(UINT64_MAX);
    csrc_mie(RISCV_MIE_MTIE);

    timer_initialized = 1;
}

void timer_start(void) {
    set_mtime(0);
    set_mtimecmp(MTIME_FREQ / 10);
    csrs_mie(RISCV_MIE_MTIE);
}

/**
 * timer_intr_handler - Serviices an interrupt request raised by the RISC-V timer.
 * Broadcasts conditions every tenth of a second and every second so that time-based
 * events occur. 
 */
void timer_intr_handler(void) {
    // Get the current system elapsed time in clock cycles
    uint64_t clock_cycles = get_mtime();

    // If more second have passed than the 1Hz count, increment it and broadcast.
    if(clock_cycles / MTIME_FREQ > tick_1Hz_count){
        tick_1Hz_count++;
        condition_broadcast(&tick_1Hz);
    }

    // If more second have passed than the 10Hz count, increment it and broadcast.
    if(clock_cycles / (MTIME_FREQ/10) > tick_10Hz_count){
        tick_10Hz_count++;
        condition_broadcast(&tick_10Hz);
    }

    // Set the new mtcmp value so that the interrupt triggers after 1/10 of a second.
    set_mtimecmp(get_mtimecmp() + MTIME_FREQ/10);

}

// Hard-coded MTIMER device addresses for QEMU virt device

#define MTIME_ADDR 0x200BFF8
#define MTCMP_ADDR 0x2004000

static inline uint64_t get_mtime(void) {
    return *(volatile uint64_t*)MTIME_ADDR;
}

static inline void set_mtime(uint64_t val) {
    *(volatile uint64_t*)MTIME_ADDR = val;
}

static inline uint64_t get_mtimecmp(void) {
    return *(volatile uint64_t*)MTCMP_ADDR;
}

static inline void set_mtimecmp(uint64_t val) {
    *(volatile uint64_t*)MTCMP_ADDR = val;
}