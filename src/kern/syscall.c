#include "trap.h"
#include "console.h"
#include "scnum.h"
#include "thread.h"
#include "memory.h"
#include "process.h"
#include "device.h"
#include "process.h"
#include "fs.h"
#include "timer.h"

const void syscall_handler(struct trap_frame * tfr);
const int64_t syscall(struct trap_frame * tfr);

static int sysexit(void);
static int sysmsgout(const char * msg);
static int sysdevopen(int fd, const char *name, int instno);
static int sysfsopen(int fd, const char *name);
static long sysclose(int fd);
static long sysread(int fd, void *buf, size_t bufsz);
static long syswrite(int fd, const void *buf, size_t len);
static int sysioctl(int fd, int cmd, void *arg);
static int sysexec(int fd);
static int sysfork(const struct trap_frame * tfr);
static int sysusleep(unsigned long us);
static int syswait(int tid);

static long verify_fd(int fd);

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
        case SYSCALL_DEVOPEN:
            return sysdevopen((int)(regs[TFR_A0]), (const char*)(regs[TFR_A1]), (int)(regs[TFR_A2]));
        case SYSCALL_FSOPEN:
            return sysfsopen((int)(regs[TFR_A0]), (const char*)(regs[TFR_A1]));
        case SYSCALL_CLOSE:
            return sysclose((int)(regs[TFR_A0]));
        case SYSCALL_READ:
            return sysread((int)(regs[TFR_A0]), (void*)(regs[TFR_A1]), (size_t)(regs[TFR_A2]));
        case SYSCALL_WRITE:
            return syswrite((int)(regs[TFR_A0]), (const void*)(regs[TFR_A1]), (size_t)(regs[TFR_A2]));
        case SYSCALL_IOCTL:
            return sysioctl((int)(regs[TFR_A0]), (int)(regs[TFR_A1]), (void *)(regs[TFR_A2]));
        case SYSCALL_EXEC:
            return sysexec((int)(regs[TFR_A0]));
        case SYSCALL_FORK:
            return sysfork(tfr);
        case SYSCALL_USLEEP:
            return sysusleep((int)(regs[TFR_A0]));
        case SYSCALL_WAIT:
            return syswait((unsigned long)(regs[TFR_A0]));
        default:
            return 0;
            break;
    }

    return 0;
}

static int sysexit(void) {
    kprintf("Process exiting.\n");
    process_exit();
}

static int sysmsgout(const char * msg) {
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

static int sysdevopen(int fd, const char *name, int instno){
    struct process * process = current_process();
    if(fd >= PROCESS_IOMAX){
        kprintf("fd over max process\n");
        return -EINVAL;
    }
    if(fd >= 0 && process->iotab[fd] != NULL){
        kprintf("fd already present\n");
        return -EINVAL;
    }

    struct io_intf * devio;
    int i = 0;
    for(i = 0; i < PROCESS_IOMAX; i++){
        if(process->iotab[i] == NULL){
            break;
        }
    }

    if(i >= PROCESS_IOMAX){
        kprintf("no open io slots\n");
        return -EINVAL;
    }

    int result = device_open(&devio, name, instno);
    if(result < 0){
        return -EINVAL;
    }
    process->iotab[fd < 0 ? i : fd] = devio;
    return fd < 0 ? i : fd;
}

static int sysfsopen(int fd, const char *name){
    struct process * process = current_process();
    if(fd >= PROCESS_IOMAX){
        kprintf("fd over max process\n");
        return -EINVAL;
    }
    if(fd >= 0 && process->iotab[fd] != NULL){
        kprintf("fd already present\n");
        return -EINVAL;
    }

    struct io_intf * fsio;
    int i = 0;
    for(i = 0; i < PROCESS_IOMAX; i++){
        if(process->iotab[i] == NULL){
            break;
        }
    }

    if(i >= PROCESS_IOMAX){
        kprintf("no open io slots\n");
        return -EINVAL;
    }

    int result = fs_open(name, &fsio);
    if(result < 0){
        return -EINVAL;
    }
    process->iotab[fd < 0 ? i : fd] = fsio;
    return fd < 0 ? i : fd;
}

static long sysclose(int fd){
    struct process * process = current_process();
    long verify = verify_fd(fd);
    if(verify < 0){
        return verify;
    }

    struct io_intf* devio = process->iotab[fd];
    ioclose(devio);
    process->iotab[fd] = NULL;
    return 0;
}

static long sysread(int fd, void *buf, size_t bufsz){
    struct process * process = current_process();
    long verify = verify_fd(fd);
    if(verify < 0){
        return verify;
    }

    struct io_intf* devio = process->iotab[fd];
    long bytes_read = ioread(devio, buf, bufsz);
    kprintf("Read %ld bytes from file.\n", bytes_read);
    return bytes_read;
}

static long syswrite(int fd, const void *buf, size_t len){
    struct process * process = current_process();
    long verify = verify_fd(fd);
    if(verify < 0){
        return verify;
    }

    struct io_intf* devio = process->iotab[fd];
    long bytes_wrote = iowrite(devio, buf, len);
    kprintf("Wrote %ld bytes to file.\n", bytes_wrote);
    return bytes_wrote;
}

static int sysioctl(int fd, int cmd, void *arg){
    struct process * process = current_process();
    int verify = verify_fd(fd);
    if(verify < 0){
        return verify;
    }

    struct io_intf* devio = process->iotab[fd];
    long result = ioctl(devio, cmd, arg);
    return result;
}

static int sysexec(int fd){
    struct process * process = current_process();
    int verify = verify_fd(fd);
    if(verify < 0){
        process_exit();
    }

    struct io_intf* exeio = process->iotab[fd];
    process->iotab[fd] = NULL;

    process_exec(exeio);
    return -EINVAL;
}

/**
 * Name: sysfork
 *
 * Inputs:
 * const struct trap_frame* - The trap frame of the currently running process at the time of the syscall.
 *
 * Outputs:
 * int - Returns the PID of the newly created child process to the parent, and 0 to the child.
 *
 * Purpose:
 * Invokes process_fork to create a new child process that continues execution at the same point.
 *
 * Side effects:
 * Allocates a new process and thread for the child and updates process management data structures.
 */
static int sysfork(const struct trap_frame * tfr){
    //TODO CP3: do this
    int child_id = process_fork((struct trap_frame *)tfr);
    return child_id;
}

/**
 * Name: sysusleep
 *
 * Inputs:
 *  unsigned long us - Number of microseconds to sleep.
 *
 * Outputs:
 *  int - Return 0 on success
 *
 * Purpose:
 *  Suspend current thread for the inputted time.
 *
 * Side effects:
 *  Delays execution of the current thread for the inputted time.
 */
static int sysusleep(unsigned long us){
    //TODO CP3: sysusleep
    struct alarm al;
    // Use function from timer.h to sleep 
    alarm_init(&al, "syslusleep");
    alarm_sleep_us(&al,us);
    // Return 0 if success
    return 0;
}

/**
 * Name: syswait
 *
 * Inputs:
 *  int tid - Thread ID to wait for
 *
 * Outputs:
 *  int - 0 on success, -1 if fails.
 *
 * Purpose:
 * If tid is main thread, wait for any child of current thread to exit. If not, wait for a specific
 * child thread to exit.
 *
 * Side effects:
 * If tid is main thread, call thread_join_any from thread.c. If not, call thread_join to wait for a 
 * specific child thread. 
 */
static int syswait(int tid){
    // TODO CP3: yep and do syswait too
    // First check if tid is main thread. Then wait for any child to exit.
    if (tid == 0){
        return thread_join_any();
    }
    else{
        // If tid is not main thread, wait for specific child to exit
        return thread_join(tid);
    }
    return -EINVAL;
}

static long verify_fd(int fd){
    struct process * process = current_process();
    if(fd >= PROCESS_IOMAX){
        kprintf("fd over max process\n");
        return -EINVAL;
    }
    if(fd < 0){
        kprintf("invalid file descriptor\n");
        return -EINVAL;
    }
    if(process->iotab[fd] == NULL){
        kprintf("fd not present\n");
        return -EINVAL;
    }
    return 0;
}

