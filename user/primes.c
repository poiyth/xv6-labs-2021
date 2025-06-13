#include "kernel/types.h"
#include "user/user.h"

//递归的创建子进程
void get_primes(int *p)
{
    //先将左边集合创建的管道写关闭，改为只读
    close(p[1]);

    //从左边接收数字
    int org;
    if(read(p[0], &org, 4) == 0) //左边集合的管道数字写已关闭
    {
        close(p[0]);//关闭左边集合的读
        exit(0);
    }
    //输出该质数
    printf("prime %d\n", org);

    //接着往后接收看有无需要传送的数字
    int x = -1;
    while(read(p[0], &x, 4) != 0)
    {
        if(x%org == 0) continue;
        break;
    }
    if(x == -1 || x%org == 0) 
    {
        close(p[0]);
        exit(0);

    }

    //创建自己和右边集合的管道
    int ps[2];
    pipe(ps);

    //创建右边的集合
    if(fork() == 0)
    {
        get_primes(ps);
    }
    else
    {
        close(ps[0]);//该管道只需写即可
        write(ps[1], &x, 4);
        while(read(p[0], &x, 4) != 0)
        {
            if(x%org ==0) continue;
            write(ps[1], &x, 4);
        }
        //关闭右边集合的写，方便最后一个进程退出
        close(ps[1]);

        //等待子进程
        //注意这里关闭左集合读和等待子进程结束的顺序！！！
        wait(0);
        
        //关闭左边集合的读
        close(p[0]);
        exit(0);
    }
    return;
}

int main()
{
    //创建管道，这个管道为主程序给第一个2进程送数据的管道
    int p[2];
    pipe(p); //p[0]读，p[1]
    //子进程
    if(fork() == 0)
    {
        get_primes(p);
    }
    else
    {
        //父进程只写，关闭读
        close(p[0]);
        int i;
        for(i=2; i<=35; ++i)
        {
            write(p[1], &i, 4);
        }
        //写完以后关闭写进程
        close(p[1]);
        //等待子进程结束
        wait(0);
    }
    exit(0);
}