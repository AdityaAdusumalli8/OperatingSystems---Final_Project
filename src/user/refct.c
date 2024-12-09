#include "syscall.h"
#include "string.h"

void main(void) {
    int result;

    result = _fsopen(1, "notepad.txt");
    if (result < 0) {
        _msgout("_fsopen failed");
        _exit();
    }
    if(_fork() != 0){
        _close(1);
        _msgout("Thread parent closed file.");
        _wait(0);
        _msgout("Parent exiting.");
        _exit();
    }
    else{
        int loc = 0;
        _usleep(100000);
        // _ioctl(0, 4, &loc);
        const char * text = "Hello World!";
        _write(1, text, strlen(text));
        
        // _ioctl(0, 4, &loc);

        char buf[32];
        // memset(buf, 0, 32);
        _read(1, buf, 32);
        _msgout(buf);
        _msgout("Child exiting.");
        _close(1);
        _exit();
    }
}
