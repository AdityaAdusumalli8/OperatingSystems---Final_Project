#include "trap.h"
#include "console.h"
#include "scnum.h"
#include "thread.h"
#include "memory.h"
#include "process.h"

const void syscall_handler(struct trap_frame * tfr);
const int64_t syscall(struct trap_frame * tfr);

// Handles syscall
void syscall_handler(struct trap_frame * tfr){
    tfr->sepc += 4;

    tfr->x[TFR_A0] = syscall(tfr);
}

int64_t syscall(struct trap_frame * tfr){
    const uint64_t * const regs = tfr->x;

    switch(regs[TFR_A7]){
        case SYSCALL_EXIT:
            return sysexit();
            break;
        case SYSCALL_MSGOUT:
            return sysmsgout((const char*)(regs[TFR_A0]));
            break;
        default:
            return 0;
            break;
    }

    return 0;
}

int sysexit(void) {
    process_exit();
}

int sysmsgout(const char * msg) {
    int result;
    trace("%s(msg=%p)", __func__, msg);
    result = memory_validate_vstr(msg, PTE_U);
    if (result != 1){
        return result;
    }
    kprintf("Thread <%s:%d> says: %s\n",
        thread_name(running_thread()),
        running_thread(), msg);
    return 0;
}