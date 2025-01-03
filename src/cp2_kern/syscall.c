#include "trap.h"
#include "console.h"
#include "scnum.h"
#include "thread.h"
#include "memory.h"
#include "process.h"
#include "device.h"
#include "process.h"
#include "fs.h"

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
            return sysread((int)(regs[TFR_A0]), (const void*)(regs[TFR_A1]), (size_t)(regs[TFR_A2]));
        case SYSCALL_WRITE:
            return syswrite((int)(regs[TFR_A0]), (const void*)(regs[TFR_A1]), (size_t)(regs[TFR_A2]));
        case SYSCALL_IOCTL:
            return sysioctl((int)(regs[TFR_A0]), (int)(regs[TFR_A1]), (void *)(regs[TFR_A2]));
        case SYSCALL_EXEC:
            return sysexec((int)(regs[TFR_A0]));
        default:
            return 0;
            break;
    }

    return 0;
}

static int sysexit(void) {
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
        return -1;
    }
    if(fd >= 0 && process->iotab[fd] != NULL){
        kprintf("fd already present\n");
        return -1;
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
        return -1;
    }

    int result = device_open(&devio, name, instno);
    if(result < 0){
        return -1;
    }
    process->iotab[fd < 0 ? i : fd] = devio;
    return fd < 0 ? i : fd;
}

static int sysfsopen(int fd, const char *name){
    struct process * process = current_process();
    if(fd >= PROCESS_IOMAX){
        kprintf("fd over max process\n");
        return -1;
    }
    if(fd >= 0 && process->iotab[fd] != NULL){
        kprintf("fd already present\n");
        return -1;
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
        return -1;
    }

    int result = fs_open(name, &fsio);
    if(result < 0){
        return -1;
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
}

static long verify_fd(int fd){
    struct process * process = current_process();
    if(fd >= PROCESS_IOMAX){
        kprintf("fd over max process\n");
        return -1;
    }
    if(fd < 0){
        kprintf("invalid file descriptor\n");
        return -1;
    }
    if(process->iotab[fd] == NULL){
        kprintf("fd not present\n");
        return -1;
    }
    return 0;
}

