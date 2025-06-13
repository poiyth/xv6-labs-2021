    #include "kernel/types.h"
    #include "user/user.h"
    #include "kernel/param.h"

    int main(int argc, char *argv[])
    {
        char *arg[MAXARG];

        if(argc > MAXARG)
        {
            write(1, "Error, The arguments are too long!\n", 35);
            exit(0);
        }

        //初始化exec参数
        arg[0] = argv[1];
        int i;
        for(i = 2; i < argc; ++i)
        {
            arg[i-1] = argv[i];
        }
        
        // 每次从上一个命令的标准输出中读取作为参数
        char onec;
        char buf[64];
        int n, id = 0;
        while((n = read(0, &onec, 1)) != 0)
        {
            if(onec == '\n') 
            {
                arg[argc - 1] = buf;
                arg[argc] = 0;
                if(fork() == 0)
                {
                    if(exec(arg[0], arg) == -1)
                    {
                        write(1, "error exec failed:", 18);
                        write(1, buf, sizeof(buf));
                        write(1, "\n", 1);
                    }
                    exit(0);
                }
                wait(0);
                id = 0;
                memset(buf, 0, sizeof(buf));
                continue;
            }
            buf[id++] = onec;
            
        }
        exit(0);
    }