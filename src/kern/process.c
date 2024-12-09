// process.c - user process
//
#include "process.h"
#include "memory.h"
#include "elf.h"
#include "thread.h"
#include "console.h"
#include "io.h"
#include "heap.h"
#include "trap.h"
#include "thread.h"
#include "halt.h"

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

// Creates the child process without an associated thread
int process_fork(struct trap_frame * tfr){
    int new_pid;
    for(new_pid = 0; new_pid < NPROC; new_pid++){
        if(proctab[new_pid] == NULL){
            break;
        }
    }
    if(new_pid >= NPROC){
        return -1;
    }
    struct process * new_process = (struct process *)kmalloc(sizeof(struct process));
    new_process->id = new_pid;

    //TODO CP3: process fork here!
    uintptr_t new_mtag = memory_space_clone(0);
    new_process->mtag = new_mtag;

    struct process * process = current_process();
    for(int i = 0; i < PROCESS_IOMAX; i++){
        new_process->iotab[i] = process->iotab[i];
        if(process->iotab[i] != NULL){
            ioref(process->iotab[i]);
        }
    }

    int child_tid = thread_fork_to_user(new_process, tfr);
    kprintf("thread fork returned\n");
    return new_pid;
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