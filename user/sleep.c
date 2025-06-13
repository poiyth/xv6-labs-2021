#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    if(argc != 2) 
    {
        write(1, "Sorry, you should pass an argument!", 35);
    }
    else
    {
        int len = atoi(argv[1]);
        sleep(len);
    }
    exit(0);
}