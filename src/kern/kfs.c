#include "fs.h"
#include "io.h"
#include "console.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BLOCK_SIZE 4096
#define MAX_FILE_SIZE (12*BLOCK_SIZE)
#define MAX_FILES 63
#define FD_USED 1
#define MAX_NAME_SIZE 32 //(bytes)

// Create file descriptor struct
typedef struct file_desc
{
    struct io_intf io;
    uint64_t file_pos;
    uint64_t file_size;
    uint32_t inode_num;
    uint32_t flags;
} file_t;


typedef struct dentry
{
    char f_name[MAX_NAME_SIZE];
    uint32_t inode_idx;
    char padding[28];
} f_dentry;

typedef struct
{
    uint32_t num_dentries;
    uint32_t num_inodes;
    uint32_t num_blocks;
    char reserved[52];
} stat_block_t;

char fs_initialized = 0;
struct io_intf* mountedIO = NULL;
static file_t fileDescriptorsArray[MAX_FILES];
static stat_block_t stat_block;
static f_dentry d_entries[MAX_FILES];

void fs_init(void)
{
    if (!fs_initialized)
    {
        fs_initialized = 1;
        memset(fileDescriptorsArray, 0, sizeof(fileDescriptorsArray)); 
        mountedIO = NULL;
    }
}

static const struct io_ops fs_io_ops = {
    .close = fs_close,
    .read = fs_read,
    .write = fs_write,
    .ctl = fs_ioctl,
};

/*
Inputs: *io - Pointer to struct representing blockdevice

Outputs: int - -1 if filesystem or device not initialized, 0 if mount successful

Effect: Assigns the pointer to block device to mountedIO, this essentially mounts the
filesystem. Prepares the filesystem for future operations.

Purpose: The purpose of this function is to mount the filesystem by providing the 
necessary block device. This allows for further filesystem operations like read and write.

*/
uint8_t statsBuffer[BLOCK_SIZE];
int fs_mount(struct io_intf *io){
    // Check if filesystem and io are initialized properly.
    if (!fs_initialized || io == NULL){
        return -EINVAL; 
    }
    // Set the mountedIO variable to the device block.
    mountedIO = io;
    ioseek(mountedIO, 0);

    long bytes_read = ioread(mountedIO, statsBuffer, BLOCK_SIZE);
    if(bytes_read != BLOCK_SIZE){
        return -EINVAL;
    }

    memcpy(&stat_block, statsBuffer, sizeof(stat_block_t));

    if(stat_block.num_dentries > MAX_FILES){
        return -EINVAL;
    }

    kprintf("Mounting file system with %d dentries, %d inodes, and %d blocks.\n", stat_block.num_dentries, stat_block.num_inodes, stat_block.num_blocks);
    memcpy(d_entries, statsBuffer + sizeof(stat_block_t), stat_block.num_dentries * sizeof(f_dentry));
    // Return Success
    return 0;
}

/*
Inputs: *io - Double Pointer to io_intf structure, will eventually point to io_intf interface of opened file
        *name - Name of the file to be opened
Outputs: int - -1 if invalid values or no avilable file descriptor, 0 if open successful

Effect: Searches and initializes a file descriptor to represent the opened file. Checks to ensure a file
descript is available and no invalid values. Set the io_intf interface of opened file with
function pointers. Modify inputed parameter to be set to the io_intf structure of opened file.

Purpose: The purpose of this function is to allow the user to open a file by name and access the
operations in the io_intf interface. Enables interactions with an opened file.
*/
int fs_open(const char *name, struct io_intf **io){
    // Perform checks to ensure the filesystem is initialized and the necessary variables have valid values.
    if (!fs_initialized || mountedIO == NULL || name == NULL || io == NULL)
    {
        return -EINVAL; 
    }
    // console_printf("%x IS MOUNTED IO\n", *mountedIO);

    // Find an available file descriptor to represent the opened file
    int availablefdIndex = -1;
    int i =0;
    while (i < MAX_FILES){
        // Check if file descriptor in use
        if (fileDescriptorsArray[i].flags == 0) {
            availablefdIndex = i; // Found available file descriptor
            fileDescriptorsArray[i].flags = FD_USED; // Set flag to 1 (in use)
            break;
        }
        i++;
    }
    if (availablefdIndex == -1){
        return -1; // No file descriptors avilable
    }

    // Initialize file descriptpr:
    file_t *newFileDescriptor = &fileDescriptorsArray[availablefdIndex];
    // Set the io_intf interface with function pointers for various file operations.
    newFileDescriptor->io.ops = &fs_io_ops;

    // Set position in the file to 0
    newFileDescriptor->file_pos = 0;
    // Find the correct dentry for the file.
    // Loop through the dentries until we find the correct file
    int found = 0;
    // console_printf("%d dentries\n", stat_block.num_dentries);
    for(uint64_t i = 0; i < MAX_FILES; i++){
        // loop through the dentries
        // find the dentry with the matching name
        f_dentry dir_entry = d_entries[i];
        // console_printf("%d: %s\n", i, dir_entry.f_name);
        if(strcmp(dir_entry.f_name, name) == 0){
            newFileDescriptor->inode_num = dir_entry.inode_idx;
            found = 1;
            break;
        }
    }
    // console_printf("found=%d\n", found);
    if(found == 0){
        // No corresponding name
        return -EINVAL;
    }

    uint32_t file_len;
    ioseek(mountedIO, 4096 * (newFileDescriptor->inode_num + 1));
    ioread(mountedIO, &file_len, 4);
    newFileDescriptor->file_size = file_len;

    // Modify pointer to contain the io_intf structure of the opened file
    *io = &(newFileDescriptor->io);

//     typedef struct file_desc
// {
//     struct io_intf io;
//     uint64_t file_pos;
//     uint64_t file_size;
//     uint32_t inode_num;
//     uint32_t flags;
// } file_t;

    console_printf("Loaded file %s with size %d, and inode num %d.\n", name, newFileDescriptor->file_size, newFileDescriptor->inode_num);
    ioseek(&(newFileDescriptor->io), 0);
    // Return success
    return 0;
}

/*
Inputs: *io - Pointer to io_intf structure representing opened file
Outputs: None

Effect: Marks the file descriptor of closed file as unused and clears the io_intf interface. 

Purpose: The purpose of this function is to allow the user to close an opened file and
clear the functions in its io_intf interface.
*/
void fs_close(struct io_intf *io)
{
    // If io is invalid then return
    if (io == NULL){
        return; 
    }

    // Find the file descriptor associated with the given io_intf
    file_t *closeFileDescriptor = (void *)io - offsetof(file_t, io);
    closeFileDescriptor->flags = 0;
    closeFileDescriptor->io.ops = NULL;
}

/**
 * Name: fs_write
 * 
 * Inputs:
 *  struct io_intf *    -> io
 *  const void *        -> buf
 *  unsigned long       -> n
 * 
 * Outputs:
 *  long                -> number of written bytes / error code
 * 
 * Purpose:
 *  The purpose of this function is to write to the file system by taking a file (io) and 
 *  writing to it.
 * 
 * Side effects:
 *  We are writing to a file, which may cause unexpected behaviour if we do not mean to. This
 *  also uses the io, which may enable interrupts as change certain registers.
 */
long fs_write(struct io_intf *io, const void *buf, unsigned long n)
{
    //Check if input parameters have valid values.
    if (io == NULL || buf == NULL || n == 0){
        return -1; 
    }

    // Find the file descriptor 
    file_t *writeFileDescriptor = (void *)io - offsetof(file_t, io);
    // Check if the size written fits within max file size
    long size_limit = writeFileDescriptor->file_size - writeFileDescriptor->file_pos;
    if (n > size_limit){
        n = size_limit;
    }

    long wroteBytes = 0;
    uint64_t buf_offset = 0;

    // Calculate the offset to the inode of the file
    uint32_t inode_num = writeFileDescriptor->inode_num;
    uint32_t inode_offset = (inode_num + 1) * BLOCK_SIZE;
    while(n > 0){
        // Calculate how many bytes to read
        uint32_t bytesToWrite = n;
        uint32_t block_offset = writeFileDescriptor->file_pos % BLOCK_SIZE;
        if(bytesToWrite + block_offset > BLOCK_SIZE)
        {
            bytesToWrite = BLOCK_SIZE - block_offset;
        }

        // Perform read and update the file position
        // Get the current data block to read from
        uint32_t block_num = writeFileDescriptor->file_pos / BLOCK_SIZE;
        ioseek(mountedIO, inode_offset + sizeof(uint32_t) * (block_num + 1));

        uint32_t fs_block_num = 0;
        ioread(mountedIO, &fs_block_num, sizeof(uint32_t)); 

        console_printf("Block number is %d (%d in file) with offset %d.\n", fs_block_num, block_num, block_offset);
        ioseek(mountedIO, (stat_block.num_inodes + 1 + fs_block_num) * BLOCK_SIZE + block_offset);

        long readBytesN = iowrite(mountedIO, buf + buf_offset, bytesToWrite);
        console_printf("Wrote %d bytes!\n", readBytesN);
        if (readBytesN > 0)
        {
            writeFileDescriptor->file_pos += readBytesN;
            wroteBytes += readBytesN;
            n -= readBytesN;
            buf_offset += readBytesN;
        }
        else{
            console_printf("Read 0 or fewer bytes.");
            return -EINVAL;
        }
    }
    return wroteBytes;

}

/**
 * Name: fs_read
 * 
 * Inputs:
 *  struct io_intf *    -> io
 *  void *        -> buf
 *  unsigned long       -> n
 * 
 * Outputs:
 *  long                -> number of read bytes / error code
 * 
 * Purpose:
 *  The purpose of this function is to read to the file system by taking a file (io) and 
 *  reading to it.
 * 
 * Side effects:
 *  We are reading from a file, which may cause unexpected behaviour if we do not mean to. This
 *  also uses the io, which may enable interrupts as change certain registers.
 */
long fs_read(struct io_intf *io, void *buf, unsigned long n)
{
    // Check if input parameters have valid values.
    if (io == NULL || buf == NULL || n == 0)
    {
        return -1;
    }

    console_printf("Doing a read!\n");

    // Find the file descriptor
    file_t *readFileDescriptor = (void *)io - offsetof(file_t, io);
    if(readFileDescriptor != 0){
        console_printf("Found file with size %d at inode %d.\n", readFileDescriptor->file_size, readFileDescriptor->inode_num);
    }

    // Check if the size read fits within max file size
    long size_limit_read = readFileDescriptor->file_size - readFileDescriptor->file_pos;
    if (n > size_limit_read)
    {
        n = size_limit_read;
    }

    long readBytes = 0; 
    // Calculate the offset to the inode of the file
    uint32_t inode_num = readFileDescriptor->inode_num;
    uint32_t inode_offset = (inode_num + 1) * BLOCK_SIZE;
    while(n > 0){
        // Calculate how many bytes to read
        uint32_t bytesToRead = n;
        uint32_t block_offset = readFileDescriptor->file_pos % BLOCK_SIZE;
        if(bytesToRead + block_offset > BLOCK_SIZE)
        {
            bytesToRead = BLOCK_SIZE - block_offset;
        }

        // Perform read and update the file position
        // Get the current data block to read from
        uint32_t block_num = readFileDescriptor->file_pos / BLOCK_SIZE;
        ioseek(mountedIO, inode_offset + sizeof(uint32_t) * (block_num + 1));

        uint32_t fs_block_num = 0;
        ioread(mountedIO, &fs_block_num, sizeof(uint32_t)); 

        console_printf("Block number is %d (%d in file) with offset %d.\n", fs_block_num, block_num, block_offset);
        ioseek(mountedIO, (stat_block.num_inodes + 1 + fs_block_num) * BLOCK_SIZE + block_offset);

        long readBytesN = ioread(mountedIO, buf, bytesToRead);
        console_printf("Read %d bytes!\n", readBytesN);
        if (readBytesN > 0)
        {
            readFileDescriptor->file_pos += readBytesN;
            readBytes += readBytesN;
            n -= readBytesN;
            buf += readBytesN;
        }
        else{
            console_printf("Read 0 or fewer bytes.");
            return -EINVAL;
        }
    }
    return readBytes;
}

/**
 * Name: fs_getlen
 * 
 * Inputs:
 *  file_t *    -> fd
 *  void *      -> arg
 * 
 * Outputs:
 *  int         -> error code
 * 
 * Purpose:
 *  The purpose of this function is to get the length of the file.
 * 
 * Side effects:
 *  No significant side effects.
 */
int fs_getlen(file_t *fd, void *arg){
    if (fd == NULL || arg == NULL){
        return -1;
    }
    *(uint64_t*)arg = fd->file_size;
    return 0;
}

/**
 * Name: fs_getpos
 * 
 * Inputs:
 *  file_t *    -> fd
 *  void *      -> arg
 * 
 * Outputs:
 *  int         -> error code
 * 
 * Purpose:
 *  The purpose of this function is to get the position of the file.
 * 
 * Side effects:
 *  No significant side effects.
 */
int fs_getpos(file_t *fd, void *arg){
    if (fd == NULL || arg == NULL){
        return -1;
    }
    *(uint64_t *)arg = fd->file_pos;
    return 0;
}

/**
 * Name: fs_setpos
 * 
 * Inputs:
 *  file_t *    -> fd
 *  void *      -> arg
 * 
 * Outputs:
 *  int         -> error code
 * 
 * Purpose:
 *  The purpose of this function is to set the position of the file.
 * 
 * Side effects:
 *  We are setting a register, so we may be causing errors by doing this erraneously.
 */
int fs_setpos(file_t *fd, void *arg)
{
    if (fd == NULL || arg == NULL){
        return -1;
    }
    uint64_t *set_position = (uint64_t *)arg;
    if (*set_position <= fd->file_size){
        fd->file_pos = *set_position;
        return 0;
    }
    return -EINVAL;
}

/**
 * Name: fs_getblksz
 * 
 * Inputs:
 *  file_t *    -> fd
 *  void *      -> arg
 * 
 * Outputs:
 *  int         -> error code
 * 
 * Purpose:
 *  The purpose of this function is to get the block size of the file.
 * 
 * Side effects:
 *  No significant side effects.
 */
int fs_getblksz(file_t *fd, void *arg){
    if (fd == NULL || arg == NULL){
        return -1; 
    }
    *(uint32_t *)arg = BLOCK_SIZE;
    return 0; 
}

/**
 * Name: fs_ioctl
 * 
 * Inputs
 *  struct io_intf *    -> io
 *  int                 -> cmd
 *  void *              -> arg
 * 
 * Outputs:
 *  int         -> error code
 * 
 * Purpose:
 *  The purpose of this function is to run a specific special command that is in the io.
 * 
 * Side effects:
 *  Different functions called in here may enable interrupts or access registers.
 */
int fs_ioctl(struct io_intf *io, int cmd, void *arg){
    if (io == NULL || arg == NULL){
        return -1; 
    }

    file_t *ioctl_fd =  (void *)io - offsetof(file_t, io);
    if (ioctl_fd != NULL){
        switch (cmd){
        case IOCTL_GETLEN:
            return fs_getlen(ioctl_fd, arg);
        case IOCTL_GETPOS:
            return fs_getpos(ioctl_fd, arg);
        case IOCTL_SETPOS:
            return fs_setpos(ioctl_fd, arg);
        case IOCTL_GETBLKSZ:
            return fs_getblksz(ioctl_fd, arg);
        default:
            return -1;
        }
    }

    return -1;
}