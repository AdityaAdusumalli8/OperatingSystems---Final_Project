// vioblk.c - VirtIO serial port (console)
// what the fuck is this file        

#include "virtio.h"
#include "intr.h"
#include "halt.h"
#include "heap.h"
#include "io.h"
#include "device.h"
#include "error.h"
#include "string.h"
#include "lock.h"
#include "thread.h"

// COMPILE-TIME PARAMETERS
//          

#define VIOBLK_IRQ_PRIO 1

// INTERNAL CONSTANT DEFINITIONS
//          

// VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX       1
#define VIRTIO_BLK_F_SEG_MAX        2
#define VIRTIO_BLK_F_GEOMETRY       4
#define VIRTIO_BLK_F_RO             5
#define VIRTIO_BLK_F_BLK_SIZE       6
#define VIRTIO_BLK_F_FLUSH          9
#define VIRTIO_BLK_F_TOPOLOGY       10
#define VIRTIO_BLK_F_CONFIG_WCE     11
#define VIRTIO_BLK_F_MQ             12
#define VIRTIO_BLK_F_DISCARD        13
#define VIRTIO_BLK_F_WRITE_ZEROES   14

// INTERNAL TYPE DEFINITIONS
//          

// All VirtIO block device requests consist of a request header, defined below,
// followed by data, followed by a status byte. The header is device-read-only,
// the data may be device-read-only or device-written (depending on request
// type), and the status byte is device-written.

struct vioblk_request_header {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

// Request type (for vioblk_request_header)

#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1

// Status byte values

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

// Main device structure.
// 
// FIXME You may modify this structure in any way you want. It is given as a
// hint to help you, but you may have your own (better!) way of doing things.

struct vioblk_device {
    volatile struct virtio_mmio_regs * regs;
    struct io_intf io_intf;
    uint16_t instno;
    uint16_t irqno;
    int8_t opened;
    int8_t readonly;

    // optimal block size
    uint32_t blksz;
    // current position
    uint64_t pos;
    // sizeo of device in bytes
    uint64_t size;
    // size of device in blksz blocks
    uint64_t blkcnt;

    struct {
        // signaled from ISR
        struct condition used_updated;

        // We use a simple scheme of one transaction at a time.

        union {
            struct virtq_avail avail;
            char _avail_filler[VIRTQ_AVAIL_SIZE(1)];
        };

        union {
            volatile struct virtq_used used;
            char _used_filler[VIRTQ_USED_SIZE(1)];
        };

        // The first descriptor is an indirect descriptor and is the one used in
        // the avail and used rings. The second descriptor points to the header,
        // the third points to the data, and the fourth to the status byte.

        struct virtq_desc desc[4];
        struct vioblk_request_header req_header;
        uint8_t req_status;
    } vq;

    // Block currently in block buffer
    uint64_t bufblkno;
    // Block buffer
    char * blkbuf;
};

// Device lock
struct lock vioblk_lock;

// INTERNAL FUNCTION DECLARATIONS
//          

static int vioblk_open(struct io_intf ** ioptr, void * aux);

static void vioblk_close(struct io_intf * io);

static long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz);

static long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n);

static int vioblk_ioctl (
    struct io_intf * restrict io, int cmd, void * restrict arg);

static void vioblk_isr(int irqno, void * aux);

// IOCTLs

static int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr);
static int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr);
static int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr);
static int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr);

// EXPORTED FUNCTION DEFINITIONS
//          

// Attaches a VirtIO block device. Declared and called directly from virtio.c.

// IO operations struct that holds the various operations created below
static const struct io_ops vioblk_io_ops = {
    .close = vioblk_close,
    .read = vioblk_read,
    .write = vioblk_write,
    .ctl = vioblk_ioctl,
};

/**
 * Name: vioblk_attach
 * 
 * Inputs:
 *  volatile struct virtio_mmio_regs*   -> regs
 *  int                                 -> irqno
 * 
 * Outputs:
 *  void
 * 
 * Purpose:
 *  The purpose of this function is to initialize the device by negotiating features,
 *  setting up descriptors, attaching the virtqueues, and registerting the ISR and device.
 * 
 * Side effects:
 *  Enables interrupts for the device, allocates kernel memory. The ISR may be called when
 *  the device starts.
 */
void vioblk_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    // FIXME add additional declarations here if needed
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_device * dev;
    uint_fast32_t blksz;
    int result;

    assert (regs->device_id == VIRTIO_ID_BLOCK);

    // Signal device that we found a driver
    regs->status |= VIRTIO_STAT_DRIVER;
    // fence o,io
    __sync_synchronize();

    // Negotiate features. We need:
    // - VIRTIO_F_RING_RESET and
    // - VIRTIO_F_INDIRECT_DESC
    // We want:
    // - VIRTIO_BLK_F_BLK_SIZE and
    // - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs,
        enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // If the device provides a block size, use it. Otherwise, use 512.
    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    debug("%p: virtio block device block size is %lu", regs, (long)blksz);

    // Allocate initialize device struct
    dev = kmalloc(sizeof(struct vioblk_device) + blksz);
    memset(dev, 0, sizeof(struct vioblk_device));

    // FIXME Finish initialization of vioblk device here
    dev->regs = regs;
    dev->irqno = irqno;
    dev->blksz = blksz;
    dev->blkcnt = regs->config.blk.capacity;
    dev->size = dev->blkcnt * dev->blksz;
    char* blkbuf = kmalloc(dev->blksz);
    dev->blkbuf = blkbuf;
    dev->bufblkno = (uint64_t)-1;
    dev->opened = 0;
    // dev->readonly = virtio_featset_test(enabled_features, VIRTIO_BLK_F_RO);
    dev->readonly = 0;
    
    condition_init(&dev->vq.used_updated, "vioblk_used_updated");

    // Set up descriptors
    dev->vq.desc[0].addr = (uint64_t)&dev->vq.desc[1];
    dev->vq.desc[0].len = sizeof(struct virtq_desc) * 3;
    dev->vq.desc[0].flags = VIRTQ_DESC_F_INDIRECT;
    dev->vq.desc[0].next = 0;

    dev->vq.desc[1].addr = (uint64_t)&dev->vq.req_header;
    dev->vq.desc[1].len = sizeof(struct vioblk_request_header);
    dev->vq.desc[1].flags = VIRTQ_DESC_F_NEXT;
    dev->vq.desc[1].next = 1;

    dev->vq.desc[2].addr = (uint64_t)dev->blkbuf;
    dev->vq.desc[2].len = dev->blksz;
    dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;
    dev->vq.desc[2].next = 2;

    dev->vq.desc[3].addr = (uint64_t)&dev->vq.req_status;
    dev->vq.desc[3].len = sizeof(dev->vq.req_status);
    dev->vq.desc[3].flags = VIRTQ_DESC_F_WRITE;
    dev->vq.desc[3].next = 0;

    // Initialize avail and used rings
    dev->vq.avail.flags = 0;
    dev->vq.avail.idx = 0;
    dev->vq.avail.ring[0] = 0;
    
    dev->vq.used.flags = 0;
    dev->vq.used.idx = 0;

    // Attach and enable virtqueue
    virtio_attach_virtq(regs, 0, 1, (uint64_t)&dev->vq.desc[0], (uint64_t)&dev->vq.used, (uint64_t)&dev->vq.avail);
    virtio_enable_virtq(regs, 0);

    // Register the ISR and device
    intr_register_isr(irqno, VIOBLK_IRQ_PRIO, vioblk_isr, dev);

    int reg_status = device_register("blk", vioblk_open, dev);
    if (reg_status < 0) {
        kprintf("blk: device_register failed\n");
        return;
    }

    lock_init(&(vioblk_lock), "blk_lock");
 
    regs->status |= VIRTIO_STAT_DRIVER_OK;    
    // fence o,oi
    __sync_synchronize();
}

/**
 * Name: vioblk_open
 * 
 * Inputs:
 *  struct io_intf**    -> ioptr
 *  void*               -> aux
 * 
 * Outputs:
 *  int                 -> error code
 * 
 * Purpose:
 *  The purpose of this function is to prepare the device for I/O by initializing virtqueue indices, 
 *  setting up the I/O interface, and enabling the device's interrupt line.
 * 
 * Side effects:
 *  Marks the device as opened (if not already) which prevents it from being opened again.
 *  Enables interrupts for the device which mahy result in the ISR being called.
 */
int vioblk_open(struct io_intf ** ioptr, void * aux) {
    // FIXME your code here
    struct vioblk_device * dev = aux;

    if (dev->opened)
        return -EBUSY;
    dev->opened = 1;

    dev->io_intf.ops = &vioblk_io_ops;
    *ioptr = &dev->io_intf;
    dev->io_intf.refcnt = 1;

    dev->vq.avail.idx = 0;
    dev->vq.used.idx = 0;

    dev->bufblkno = (uint64_t)-1;
    dev->pos = 0;

    intr_enable_irq(dev->irqno);

    return 0;
}

// Must be called with interrupts enabled to ensure there are no pending
// interrupts (ISR will not execute after closing).

/**
 * Name: vioblk_close
 * 
 * Inputs:
 *  struct io_intf* -> io
 * 
 * Outputs:
 *  void
 * 
 * Purpose:
 *  The purpose of this function is to close the device by resetting the virtqueues and
 *  disabling the device's interrupt line.
 * 
 * Side effects:
 *  Disables interrupts for the device which would essentially prevent any future I/O from
 *  that device. Also interracts with device registers directly.
 */
void vioblk_close(struct io_intf * io) {
    // FIXME your code here
    struct vioblk_device * dev = (void*)io - offsetof(struct vioblk_device, io_intf);

    dev->opened = 0;

    dev->vq.avail.idx = 0;
    dev->vq.used.idx = 0;

    kfree(dev->blkbuf);

    intr_disable_irq(dev->irqno);
}

/**
 * Name: vioblk_read
 * 
 * Inputs:
 *  struct io_intf * restrict   -> io
 *  void * restrict             -> buf
 *  unsigned long               -> bufsz
 * 
 * Outputs:
 *  long                        -> bytes read / error code
 * 
 * Purpose:
 *  The purpose of this function is to read the specified number of bytes from the block device,
 *  handling block alignment and buffering as well as updates the current position.
 * 
 * Side Effects:
 *  May cause the calling thread to sleep while waiting for I/O completion. Interacts with device
 *  registers and relies on interrupts handling.
 */
long vioblk_read (
    struct io_intf * restrict io,
    void * restrict buf,
    unsigned long bufsz)
{
    // FIXME your code here
    // struct vioblk_device * dev = (void*)io - offsetof(struct vioblk_device, io_intf);
    // unsigned long total = 0;

    // if (dev->pos >= dev->size){
    //     return 0;
    // }
    
    // if (dev->pos + bufsz > dev->size){
    //     bufsz = dev->size - dev->pos;
    // }
    
    // while (bufsz > 0) {
    //     uint64_t blkno = dev->pos / dev->blksz;
    //     console_printf("reading blkno %d", blkno);
    //     uint64_t blkoff = dev->pos % dev->blksz;
    //     unsigned long n = dev->blksz - blkoff;
    //     if (n > bufsz) n = bufsz;

    //     if (dev->bufblkno != blkno) {
    //         dev->vq.req_header.type = VIRTIO_BLK_T_IN;
    //         dev->vq.req_header.reserved = 0;
    //         dev->vq.req_header.sector = blkno * (dev->blksz / 512);

    //         dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;

    //         dev->vq.req_status = 0xFF;

    //         dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0;
    //         __sync_synchronize();
    //         dev->vq.avail.idx++;
    //         __sync_synchronize();

    //         virtio_notify_avail(dev->regs, 0);

    //         condition_wait(&dev->vq.used_updated);

    //         if (dev->vq.req_status != VIRTIO_BLK_S_OK)
    //             return -EIO;
            
    //         dev->bufblkno = blkno;
    //     }

    //     memcpy(buf, dev->blkbuf + blkoff, n);
    //     console_printf("currently in buffer: %d\n", (dev->bufblkno));
    //     console_printf("vioblk_read copy %s\n", (char *)(dev->blkbuf + blkoff));
    //     buf = (char *)buf + n;
    //     bufsz -= n;
    //     total += n;
    //     dev->pos += n;
    // }
    // return total;
    struct vioblk_device *dev = (void *)io - offsetof(struct vioblk_device, io_intf);
    unsigned long total = 0;

    // Ensure we don't read beyond the device's available data
    if (dev->pos >= dev->size)
        return 0;
    if (dev->pos + bufsz > dev->size)
        bufsz = dev->size - dev->pos;

    while (bufsz > 0) {
        // Calculate block number and offset within the block
        uint64_t blkno = dev->pos / dev->blksz;
        uint64_t blkoff = dev->pos % dev->blksz;
        unsigned long n = dev->blksz - blkoff;
        if (n > bufsz) n = bufsz;

        // If the requested block is not cached, request it from the device
        if (dev->bufblkno != blkno) {
            // Prepare a read operation by setting the request type and sector
            dev->vq.req_header.type = VIRTIO_BLK_T_IN;
            dev->vq.req_header.sector = blkno * (dev->blksz / 512);

            dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;

            // Mark request status as "in progress" and notify the device
            // dev->vq.req_status = 0xFF;

            dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0; // Requesting descriptor 0
            __sync_synchronize();
            dev->vq.avail.idx++;
            __sync_synchronize();

            // Notify device and wait for the request to complete
            int i = intr_disable();
            virtio_notify_avail(dev->regs, 0);
            condition_wait(&dev->vq.used_updated);
            intr_restore(i);

            // Check the status of the completed request
            if (dev->vq.req_status != VIRTIO_BLK_S_OK) {
                console_printf("Read failed with status %d\n", dev->vq.req_status);
                return -EIO;
            }

            // Cache the current block number to avoid redundant requests
            dev->bufblkno = blkno;
        }

        // Copy data from the block buffer to the provided user buffer
        memcpy(buf, dev->blkbuf + blkoff, n);
        buf = (char *)buf + n;
        bufsz -= n;
        total += n;
        dev->pos += n;
    }
    return total;
}

/**
 * Name: vioblk_write
 * 
 * Inputs:
 *  struct io_intf * restrict   -> io
 *  const void * restrit        -> buf
 *  unsigned long               -> n
 * 
 * Outputs:
 *  long                        -> error code
 * 
 * Purpose:
 *  The purpose of this function is to write the specified number of bytes to the block device,
 *  handling block alignment and buffering as well as updates the current position.
 * 
 * Side effects:
 *  May cause the calling thread to sleep while waiting for I/O completion. Interacts with device
 *  registers and relies on interrupts handling. Function will fail if the device is read-only.
 */
long vioblk_write (
    struct io_intf * restrict io,
    const void * restrict buf,
    unsigned long n)
{
    // FIXME your code here
    struct vioblk_device * dev = (void*)io - offsetof(struct vioblk_device, io_intf);
    int buf_off = 0;
    unsigned long total = 0;

    if (dev->readonly) {
        return -ENOTSUP;
    }

    if (dev->pos >= dev->size) {
        return -EIO;
    }

    if (dev->pos + n > dev->size) {
        n = dev->size - dev->pos;
    }

    while (n > 0) {
        uint64_t blkno = dev->pos / dev->blksz;
        uint64_t blkoff = dev->pos % dev->blksz;
        unsigned long nbytes = dev->blksz - blkoff;
        if (nbytes > n) nbytes = n;

        if (nbytes < dev->blksz) {
            if (dev->bufblkno != blkno) {
                // dev->vq.req_header.type = VIRTIO_BLK_T_IN;
                dev->vq.req_header.type = VIRTIO_BLK_T_OUT;
                dev->vq.req_header.reserved = 0;
                dev->vq.req_header.sector = blkno * (dev->blksz / 512);

                dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;

                dev->vq.req_status = 0xFF;

                dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0;
                __sync_synchronize();
                dev->vq.avail.idx++;
                __sync_synchronize();

                virtio_notify_avail(dev->regs, 0);

                condition_wait(&dev->vq.used_updated);

                if (dev->vq.req_status != VIRTIO_BLK_S_OK) {
                    return -EIO;
                }

                dev->bufblkno = blkno;
            }
        } else {
            dev->bufblkno = blkno;
        }
        memcpy(dev->blkbuf + blkoff, buf + buf_off, nbytes);

        dev->vq.req_header.type = VIRTIO_BLK_T_OUT;
        dev->vq.req_header.reserved = 0;
        dev->vq.req_header.sector = blkno * (dev->blksz / 512);

        dev->vq.desc[2].flags = VIRTQ_DESC_F_NEXT;

        dev->vq.req_status = 0xFF;

        dev->vq.avail.ring[dev->vq.avail.idx % 1] = 0;
        __sync_synchronize();
        dev->vq.avail.idx++;
        __sync_synchronize();

        virtio_notify_avail(dev->regs, 0);

        condition_wait(&dev->vq.used_updated);

        if (dev->vq.req_status != VIRTIO_BLK_S_OK) {
            return -EIO;
        }

        buf_off += nbytes;
        n -= nbytes;
        total += nbytes;
        dev->pos += nbytes;
    }

    return total;
}

int vioblk_ioctl(struct io_intf * restrict io, int cmd, void * restrict arg) {
    struct vioblk_device * const dev = (void*)io -
        offsetof(struct vioblk_device, io_intf);
    
    trace("%s(cmd=%d,arg=%p)", __func__, cmd, arg);
    
    switch (cmd) {
    case IOCTL_GETLEN:
        return vioblk_getlen(dev, arg);
    case IOCTL_GETPOS:
        return vioblk_getpos(dev, arg);
    case IOCTL_SETPOS:
        return vioblk_setpos(dev, arg);
    case IOCTL_GETBLKSZ:
        return vioblk_getblksz(dev, arg);
    default:
        return -ENOTSUP;
    }
}

/**
 * Name: vioblk_isr
 * 
 * Inputs:
 *  int     -> irqno
 *  void *  -> aux
 * 
 * Outputs
 *  void
 * 
 * Purpose:
 *  The purpose of this function is to handle device interrupts by acknowledging them and
 *  waking up threads waiting for I/O completion.
 * 
 * Side effects:
 *  Wakes up waiting threads, potentially causing them to resume execution. Modifies device
 *  registers to acknowledge interrupts.
 */
void vioblk_isr(int irqno, void * aux) {
    // FIXME your code here
    struct vioblk_device * dev = aux;
    uint32_t isr_status = dev->regs->interrupt_status;

    if (isr_status & 0x1) {
        dev->regs->interrupt_ack = isr_status & 0x1;
        __sync_synchronize();

        condition_broadcast(&dev->vq.used_updated);
    }
}

/**
 * Name: vioblk_getlen
 * 
 * Inputs:
 *  const struct vioblk_device* -> dev
 *  uint64_t*                   -> lenptr
 * 
 * Outputs
 *  int                         -> size
 * 
 * Purpose:
 *  The purpose of this function is to return the device size via ioctl.
 * 
 * Side effects:
 *  No significant side effects.
 */
int vioblk_getlen(const struct vioblk_device * dev, uint64_t * lenptr) {
    // FIXME your code here
    if (!lenptr)
        return -EINVAL;
    *lenptr = dev->size;
    return 0;
}

/**
 * Name: vioblk_getpos
 * 
 * Inputs:
 *  constr struct vioblk_device*    -> dev
 *  uint64_t*                       -> posptr
 * 
 * Outputs
 *  int                             -> pos
 * 
 * Purpose:
 *  The purpose of this function is to return the current position in the device.
 * 
 * Side effects:
 *  No significant side effects.
 */
int vioblk_getpos(const struct vioblk_device * dev, uint64_t * posptr) {
    // FIXME your code here
    if (!posptr)
        return -EINVAL;
    *posptr = dev->pos;
    return 0;
}

/**
 * Name: vioblk_setpos
 * 
 * Inputs:
 *  struct vioblk_device*   -> dev
 *  const uint64_t*         -> posptr
 * 
 * Outputs:
 *  int                     -> error code
 * 
 * Purpose:
 *  The purpose of this function is to update the current position in the device.
 * 
 * Side effects:
 *  If the new position is invalid it will raise an eror. This also updates the device registers.
 */
int vioblk_setpos(struct vioblk_device * dev, const uint64_t * posptr) {
    // FIXME your code here
    if (!posptr || *posptr > dev->size)
        return -EINVAL;
    dev->pos = *posptr;
    return 0;
}

/**
 * Name: vioblk_getblksz
 * 
 * Inputs:
 *  const struct vioblk_device* -> dev
 *  uint32_t*                   -> blkszptr
 * 
 * Outputs:                     -> blksz
 *  int
 * 
 * Purpose:
 *  The purpose of this function is return the block size via ioctl.
 * 
 * Side effects:
 *  No significant side effects.
 */
int vioblk_getblksz (
    const struct vioblk_device * dev, uint32_t * blkszptr)
{
    // FIXME your code here
    if (!blkszptr)
        return -EINVAL;
    *blkszptr = dev->blksz;
    return 0;
}
