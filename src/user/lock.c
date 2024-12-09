#include "syscall.h"
#include "string.h"

void main(void) {
    int result;

    result = _fsopen(1, "notepad.txt");
    int pos = 0;
    _ioctl(1, 4, &pos);
    if (result < 0) {
        _msgout("_fsopen failed");
        _exit();
    }
    if(_fork() != 0){
        char * msg1 = "Message 1. ";
        _write(1, msg1, strlen(msg1));
        char * msg2 = "Message 2. ";
        _write(1, msg2, strlen(msg2));
        char * msg3 = "Message 3. ";
        _write(1, msg3, strlen(msg3));
        _wait(0);
        
        char buf[512];
        memset(buf, 0, 512);
        _ioctl(1, 4, &pos);
        _read(1, buf, 50);
        _msgout(buf);

        _exit();
    }
    else{
        char * msg1 = "Message 1. ";
        _write(1, msg1, strlen(msg1));
        char * msg2 = "Message 2. ";
        _write(1, msg2, strlen(msg2));
        char * msg3 = "Message 3. ";
        _write(1, msg3, strlen(msg3));

        _msgout("Child exiting.");
        _exit();
    }
}
