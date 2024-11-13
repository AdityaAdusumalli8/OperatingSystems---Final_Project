#include "io.h"
#include "string.h"

void main(struct io_intf * io) {
    struct io_intf * tio;
    struct io_term iot;

    int a = 1;
    int b = 2;
    int c = a + b;
    char d = c;

    tio = ioterm_init(&iot, io);
    ioputs(tio, "Hello, world!");
    ioputs(tio, &d);
}