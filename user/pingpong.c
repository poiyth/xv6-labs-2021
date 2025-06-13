#include "kernel/types.h"
#include "user/user.h"

int p[2];//代表管道的两个端口，0代表读，1表示写

int main()
{
    pipe(p);//创建管道
    //子进程
    if(fork() == 0)
    {
        //子进程首先接受一个字节数据
        char ch[64];
        read(p[0], ch, 1);
        //打印输出
        int pid = getpid();
        printf("%d: received ping\n", pid);
        //通过管道写给父进程
        write(p[1], ch, 1); 
        // close(p[1]);
    }
    else //父进程
    {
        //向子进程发送一个字节
        write(p[1], "A", 1);
        //接受子进程传递的数据
        char ch[64];
        read(p[0], ch, 1);
        // close(p[0]);
        //输出
        int pid = getpid();
        printf("%d: received pong\n", pid);
    }
    exit(0);

}