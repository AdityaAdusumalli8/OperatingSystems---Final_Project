//           main.c - Main function: runs shell to load executable
//          

#include "console.h"
#include "thread.h"
#include "device.h"
#include "uart.h"
#include "timer.h"
#include "intr.h"
#include "heap.h"
#include "virtio.h"
#include "halt.h"
#include "elf.h"
#include "fs.h"
#include "string.h"
#include "io.h"

//           end of kernel image (defined in kernel.ld)
extern char _kimg_end[];

#define RAM_SIZE (8*1024*1024)
#define RAM_START 0x80000000UL
#define KERN_START RAM_START
#define USER_START 0x80100000UL

#define UART0_IOBASE 0x10000000
#define UART1_IOBASE 0x10000100
#define UART0_IRQNO 10

#define VIRT0_IOBASE 0x10001000
#define VIRT1_IOBASE 0x10002000
#define VIRT0_IRQNO 1

static void shell_main(struct io_intf * termio);

void main(void) {
//     struct io_intf * termio;
//     struct io_intf * blkio;
//     void * mmio_base;
//     int result;
//     int i;

//     console_init();
//     intr_init();
//     devmgr_init();
//     thread_init();
//     timer_init();

//     heap_init(_kimg_end, (void*)USER_START);

//     //           Attach NS16550a serial devices

//     for (i = 0; i < 2; i++) {
//         mmio_base = (void*)UART0_IOBASE;
//         mmio_base += (UART1_IOBASE-UART0_IOBASE)*i;
//         uart_attach(mmio_base, UART0_IRQNO+i);
//     }
    
//     //           Attach virtio devices

//     for (i = 0; i < 8; i++) {
//         mmio_base = (void*)VIRT0_IOBASE;
//         mmio_base += (VIRT1_IOBASE-VIRT0_IOBASE)*i;
//         virtio_attach(mmio_base, VIRT0_IRQNO+i);
//     }

//     intr_enable();
//     timer_start();

//    result = device_open(&blkio, "blk", 0);

//     if (result != 0)
//         panic("device_open failed");
    
//     fs_init();

//     result = fs_mount(blkio);

//     debug("Mounted blk0");

//     ioseek(blkio, 0);
//     uint64_t blk_data = 0;
//     for(int i = 0; i < 8; i++){
//         ioread(blkio, &blk_data, 8);
//         console_printf("%x\n", blk_data);
//     }
//     return 0;

//     if (result != 0)
//         panic("fs_mount failed");

//     //           Open terminal for trek

//     result = device_open(&termio, "ser", 1);

//     if (result != 0)
//         panic("Could not open ser1");
    
//     shell_main(termio);
    struct io_intf * termio;
    struct io_intf * blkio;
    void * mmio_base;
    int result;
    int i;

    console_init();
    intr_init();
    devmgr_init();
    thread_init();
    timer_init();

    heap_init(_kimg_end, (void*)USER_START);

    // Attach NS16550a serial devices
    for (i = 0; i < 2; i++) {
        mmio_base = (void*)UART0_IOBASE;
        mmio_base += (UART1_IOBASE-UART0_IOBASE)*i;
        uart_attach(mmio_base, UART0_IRQNO+i);
    }

    // Attach virtio devices
    for (i = 0; i < 8; i++) {
        mmio_base = (void*)VIRT0_IOBASE;
        mmio_base += (VIRT1_IOBASE-VIRT0_IOBASE)*i;
        virtio_attach(mmio_base, VIRT0_IRQNO+i);
    }

    intr_enable();
    timer_start();

    // Open the block device
    result = device_open(&blkio, "blk", 0);
    if (result != 0)
        panic("device_open failed");

    // Initialize and mount the filesystem
    fs_init();
    result = fs_mount(blkio);
    if (result != 0)
        panic("fs_mount failed");
    console_printf("Mounted filesystem on blk device.\n");

    // Test block device read
    // THIS IS THE GIANT BLOCK THAT IS BEING READ FROM
    char blkbuf[512];
    long blk_bytes_read;

    ioseek(blkio, 0); // Seek to the beginning of the block device
    blk_bytes_read = ioread(blkio, blkbuf, sizeof(blkbuf));
    if (blk_bytes_read < 0) {
        console_printf("ioread from blk device failed with error %ld\n", blk_bytes_read);
    } else {
        console_printf("Read %ld bytes from blk device.\n", blk_bytes_read);
        // Print data in hex (arbitrary choice, ik)
        for (int i = 0; i < blk_bytes_read; i++) {
            console_printf("%02x ", (unsigned char)blkbuf[i]);
            if ((i+1)%16 == 0) console_printf("\n");
        }
        console_printf("\n");
    }

    // Test block device write (the device should not be read-only hopefully???)
    const char *blkdata = "Data to write to block device.";
    long blk_bytes_written;

    blk_bytes_written = iowrite(blkio, blkdata, strlen(blkdata));
    if (blk_bytes_written < 0) {
        console_printf("iowrite to blk device failed with error %ld\n", blk_bytes_written);
    } else {
        console_printf("Wrote %ld bytes to blk device.\n", blk_bytes_written);
    }

    ioseek(blkio, 0);
    blk_bytes_read = ioread(blkio, blkbuf, strlen(blkdata));
    if (blk_bytes_written < 0) {
        console_printf("ioread to blk device failed with error %ld\n", blk_bytes_written);
    } else {
        console_printf("Read (%ld) bytes to blk device: %s\n", blk_bytes_written, blkbuf);
    }
    blk_bytes_read = ioread(blkio, blkbuf, strlen(blkdata));
    if (blk_bytes_written < 0) {
        console_printf("ioread to blk device failed with error %ld\n", blk_bytes_written);
    } else {
        console_printf("Read (%ld) bytes to blk device: %s\n", blk_bytes_written, blkbuf);
    }

    // Test block device IOCTL commands
    uint64_t dev_length;
    result = ioctl(blkio, IOCTL_GETLEN, &dev_length);
    if (result != 0) {
        console_printf("ioctl GETLEN on blk device failed with error %d\n", result);
    } else {
        console_printf("Block device length is %llu bytes.\n", dev_length);
    }

    uint64_t dev_pos;
    result = ioctl(blkio, IOCTL_GETPOS, &dev_pos);
    if (result != 0) {
        console_printf("ioctl GETPOS on blk device failed with error %d\n", result);
    } else {
        console_printf("Block device current position is %llu bytes.\n", dev_pos);
    }

    uint32_t dev_blksz;
    result = ioctl(blkio, IOCTL_GETBLKSZ, &dev_blksz);
    if (result != 0) {
        console_printf("ioctl GETBLKSZ on blk device failed with error %d\n", result);
    } else {
        console_printf("Block device block size is %u bytes.\n", dev_blksz);
    }

    // Test filesystem functions
    struct io_intf *fileio;
    result = fs_open("testfile", &fileio);
    if (result != 0) {
        console_printf("fs_open failed with error %d\n", result);
        return;
    }
    console_printf("Opened file 'testfile'.\n");

    // Read from the file
    char buffer[128];
    long bytes_read;

    bytes_read = ioread(fileio, buffer, sizeof(buffer)-1);
    if (bytes_read < 0) {
        console_printf("ioread failed with error %ld\n", bytes_read);
    } else {
        buffer[bytes_read] = '\0'; // Null-terminate the buffer
        console_printf("Read %ld bytes from file: %s\n", bytes_read, buffer);
    }

    // Write to the file
    const char *data = "Hello, filesystem!";
    long bytes_written;

    bytes_written = iowrite(fileio, data, strlen(data));
    if (bytes_written < 0) {
        console_printf("iowrite failed with error %ld\n", bytes_written);
    } else {
        console_printf("Wrote %ld bytes to file.\n", bytes_written);
    }

    // Test filesystem IOCTL commands
    uint64_t file_length;
    result = ioctl(fileio, IOCTL_GETLEN, &file_length);
    if (result != 0) {
        console_printf("ioctl GETLEN failed with error %d\n", result);
    } else {
        console_printf("File length is %llu bytes.\n", file_length);
    }

    uint64_t file_pos;
    result = ioctl(fileio, IOCTL_GETPOS, &file_pos);
    if (result != 0) {
        console_printf("ioctl GETPOS failed with error %d\n", result);
    } else {
        console_printf("File position is %llu bytes.\n", file_pos);
    }

    uint64_t new_pos = 0;
    result = ioctl(fileio, IOCTL_SETPOS, &new_pos);
    if (result != 0) {
        console_printf("ioctl SETPOS failed with error %d\n", result);
    } else {
        console_printf("File position set to %llu bytes.\n", new_pos);
    }

    uint32_t block_size;
    result = ioctl(fileio, IOCTL_GETBLKSZ, &block_size);
    if (result != 0) {
        console_printf("ioctl GETBLKSZ failed with error %d\n", result);
    } else {
        console_printf("File block size is %u bytes.\n", block_size);
    }

    // Close the file
    fs_close(fileio);
    console_printf("Closed file 'testfile'.\n");

    // Re-open the file and read to verify data
    result = fs_open("testfile", &fileio);
    if (result != 0) {
        console_printf("fs_open failed with error %d\n", result);
        return;
    }
    console_printf("Opened file 'testfile' again.\n");

    memset(buffer, 0, sizeof(buffer));
    bytes_read = ioread(fileio, buffer, sizeof(buffer)-1);
    if (bytes_read < 0) {
        console_printf("ioread failed with error %ld\n", bytes_read);
    } else {
        buffer[bytes_read] = '\0';
        console_printf("Read %ld bytes from file: %s\n", bytes_read, buffer);
    }

    fs_close(fileio);
    console_printf("Closed file 'testfile'.\n");

    // Can uncomment this to go to the terminal if you want
    // shell_main(termio);
}

void shell_main(struct io_intf * termio_raw) {
    struct io_term ioterm;
    struct io_intf * termio;
    void (*exe_entry)(struct io_intf*);
    struct io_intf * exeio;
    char cmdbuf[9];
    int tid;
    int result;

    termio = ioterm_init(&ioterm, termio_raw);

    ioputs(termio, "Enter executable name or \"exit\" to exit");
    

    for (;;) {
        ioprintf(termio, "CMD> ");
        ioterm_getsn(&ioterm, cmdbuf, sizeof(cmdbuf));

        if (cmdbuf[0] == '\0')
            continue;

        if (strcmp("exit", cmdbuf) == 0)
            return;
        
        result = fs_open(cmdbuf, &exeio);

        if (result < 0) {
            if (result == -ENOENT)
                ioprintf(termio, "%s: File not found\n", cmdbuf);
            else
                ioprintf(termio, "%s: Error %d\n", cmdbuf, -result);
            continue;
        }

        debug("Calling elf_load(\"%s\")", cmdbuf);

        result = elf_load(exeio, &exe_entry);

        console_printf("elf_load(\"%s\") returned %d", cmdbuf, result);

        debug("elf_load(\"%s\") returned %d", cmdbuf, result);

        if (result < 0) {
            ioprintf(termio, "%s: Error %d\n", -result);
        
        } else {
            tid = thread_spawn(cmdbuf, (void*)exe_entry, termio_raw);

            if (tid < 0)
                ioprintf(termio, "%s: Error %d\n", -result);
            else
                thread_join(tid);
        }

        ioclose(exeio);
    }
}