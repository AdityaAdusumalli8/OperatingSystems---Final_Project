#include "fs.h"
#include "io.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BLOCK_SIZE 4096
#define MAX_FILE_SIZE (12*BLOCK_SIZE)
#define MAX_FILES 32
#define FD_USED 1

// Create file descriptor struct`
typedef struct file_desc
{
    struct io_intf io;
    uint64_t file_pos;
    uint64_t file_size;
    uint32_t inode_num;
    uint32_t flags;
} file_desc_t;

int fileSystemInit = 0;
struct io_intf* mountedIO = NULL;
static file_desc_t fileDescriptorsArray[MAX_FILES];

void fs_init(void)
{
    if (!fileSystemInit)
    {
        fileSystemInit = 1;
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
// TODO: change errors
int fs_mount(struct io_intf *io){
    // Check if filesystem and io are initialized properly.
    if (!fileSystemInit || io == NULL){
        // Error
        return -1; //
    }
    else{
        // Set the mountedIO variable to the device block.
        mountedIO = io;
    }
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
    if (!fileSystemInit || mountedIO == NULL || name == NULL || io == NULL)
    {
        return -1; 
    }

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
    file_desc_t *newFileDescriptor = &fileDescriptorsArray[availablefdIndex];
    // Set the io_intf interface with function pointers for various file operations.
    newFileDescriptor->io.ops = &fs_io_ops;

    // Modify pointer to contain the io_intf structure of the opened file
    *io = &(newFileDescriptor->io);
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
    for (int i = 0; i < MAX_FILES; i++){
        if (&(fileDescriptorsArray[i].io) == io)
        { 
            fileDescriptorsArray[i].flags = 0; // Mark as unused

            // Set the io_intf function pointers to null
            fileDescriptorsArray[i].io.ops = NULL;

            return; 
        }
    }
}

/*
*/
long fs_write(struct io_intf *io, const void *buf, unsigned long n)
{
    //Check if input parameters have valid values.
    if (io == NULL || buf == NULL || n == 0){
        return -1; 
    }

    // Find the file descriptor 
    file_desc_t *writeFileDescriptor = NULL; 
    for (int i = 0; i < MAX_FILES; i++){
        if (&(fileDescriptorsArray[i].io) == io)
        {
            // Initialize the file desriptot
            writeFileDescriptor = &fileDescriptorsArray[i];
            // Check if the size written fits within max file size
            long size_limit = MAX_FILE_SIZE - writeFileDescriptor->file_pos;
            if (n > size_limit){
                n = size_limit;
            }

            // Perform write and update the file position
            long writtenBytes = iowrite(io, buf, n);
            if (writtenBytes>0){
                writeFileDescriptor->file_pos += writtenBytes;
            }
            return writtenBytes;
        }

    }
    return -1;

}

/*
 */
long fs_read(struct io_intf *io, void *buf, unsigned long n)
{
    // Check if input parameters have valid values.
    if (io == NULL || buf == NULL || n == 0)
    {
        return -1;
    }

    // Find the file descriptor
    file_desc_t *readFileDescriptor = NULL;
    for (int i = 0; i < MAX_FILES; i++)
    {
        if (&(fileDescriptorsArray[i].io) == io)
        {
            // Initialize the file desriptot
            readFileDescriptor = &fileDescriptorsArray[i];
            // Check if the size read fits within max file size
            long size_limit_read = readFileDescriptor->file_size - readFileDescriptor->file_pos;
            if (n > size_limit_read)
            {
                n = size_limit_read;
            }

            // Perform read and update the file position
            long readBytes = ioread(io, buf, n);
            if (readBytes > 0)
            {
                readFileDescriptor->file_pos += readBytes;
            }
            return readBytes;
        }
    }
    return -1;
}

int fs_ioctl(struct io_intf *io, int cmd, void *arg){
    if (io == NULL || arg == NULL){
        return -1; 
    }

    file_desc_t *ioctl_fd = NULL;
    for (int i = 0; i < MAX_FILES; i++){
        if (&fileDescriptorsArray[i].io == io ){
            ioctl_fd = &fileDescriptorsArray[i];
            break;
        }
    }
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

int fs_getlen(file_desc_t *fd, void *arg){
    if (fd == NULL || arg == NULL){
        return -1;
    }
    uint64_t *recieve_length = (uint64_t *)arg;
    *recieve_length = fd->file_size;
    return 0;
}

int fs_getpos(file_desc_t *fd, void *arg){
    if (fd == NULL || arg == NULL){
        return -1;
    }
    uint64_t *recieve_pos = (uint64_t *)arg;
    *recieve_pos = fd->file_pos;
    return 0;
}

int fs_setpos(file_desc_t *fd, void *arg)
{
    if (fd == NULL || arg == NULL){
        return -1;
    }
    uint64_t *set_position = (uint64_t *)arg;
    if (*set_position <= fd->file_size){
        fd->file_pos = *set_position;
        return 0;
    }
    return -1;
}

int fs_getblksz(file_desc_t *fd, void *arg){
    if (fd == NULL || arg == NULL){
        return -1; 
    }
    uint32_t *blksz_ptr = (uint32_t *)arg;

    *blksz_ptr = BLOCK_SIZE;

    return 0; 
}