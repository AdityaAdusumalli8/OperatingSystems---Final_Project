// process.c - user process
//
#include "process.h"
#include "memory.h"
#include "elf.h"
#include "thread.h"
#include "console.h"
#include "io.h"

#ifdef PROCESS_TRACE
#define TRACE
#endif

#ifdef PROCESS_DEBUG
#define DEBUG
#endif


// COMPILE-TIME PARAMETERS
//

// NPROC is the maximum number of processes

#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_PID 0

// The main user process struct

static struct process main_proc;

// A table of pointers to all user processes in the system

struct process * proctab[NPROC] = {
    [MAIN_PID] = &main_proc
};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

// Initializes the process manager by retroactively creating a process
// around the currently running thread and memory space.
void procmgr_init(void){
    // Code from lecture slides on processes
    main_proc.id = MAIN_PID;
    main_proc.tid = running_thread();
    main_proc.mtag = active_memory_space();
    thread_set_process(main_proc.tid, &main_proc);
}

int process_exec(struct io_intf *exeio){
    memory_unmap_and_free_user();
    void (*entryptr)(void);
    int status = elf_load(exeio, &entryptr);
    if(status < 0){
        panic("ELF_LOAD FAILED!!!!!!!");
    }
    thread_jump_to_user(USER_STACK_VMA, (uintptr_t)entryptr);
    process_exit();
}

int process_fork(struct process * process){
    //TODO CP3: process fork here!
    return 0;
}

void process_exit(void){
    memory_unmap_and_free_user();
    struct process * process = current_process();
    for(int i = 0; i < PROCESS_IOMAX; i++){
        if(process->iotab[i] != NULL){
            ioclose(process->iotab[i]);
        }
    }
    thread_exit();
}