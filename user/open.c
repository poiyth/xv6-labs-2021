#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int main()
{
    int fd = open("output.txt", O_WRONLY | O_CREATE);
    write(fd, "Hello World!\n", 13);
    exit(0);
}