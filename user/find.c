#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* fmtname(char *path)
{
    //用static定义一块文件名称内存，方便指针指向
    static char buf[DIRSIZ + 1];
    char *p;
    for(p = path + strlen(path); p >= path && *p != '/'; p--) ;
    p++;   //p指向该文件的第一个字母
    if(strlen(p) >= DIRSIZ) return p;
    strcpy(buf, p);
    buf[strlen(p)] = 0;
    return buf;
}

//find函数表示从当前path目录及其子目录中所有包含fname的文件
void find(char *path, char *fname)
{
    // printf("path=%s, fname=%s\n", path, fname);
    int fd;
    //这个结构体可以记录文件信息
    struct stat st;
    
    // path[strlen(path)] = 0;
    //如果没打开文件报错返回
    if((fd = open(path, 0)) < 0)
    {
        write(1, "find: cannot open ", 18);
        write(1, path, strlen(path));
        write(1, "\n", 1);
        return;
    }
    //获取该文件的信息
    if(fstat(fd, &st) < 0)
    {
        write(1, "find: cannot stat ", 18);
        write(1, path, strlen(path));
        write(1, "\n", 1);
        close(fd);
        return;
    }

    //缓存
    char buf[512], *p;//这两个数组操作path方便替换
    struct dirent de;

    switch(st.type)
    {
        case T_DIR:
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf))
            {
                write(1, "find: path too long", 19);
                write(1, "\n", 1);
                break;
            }
            strcpy(buf, path);  //将path的值交给buf
            p = buf + strlen(buf);   //p指向buf的下一个没有值的地方
            *p++ = '/'; //给地址加上一个'/'
            //目录是个特殊的文件，内容包含一个个dirent，只需要一个一个读即可.
            while(read(fd, &de, sizeof(de)) == sizeof(de))    
            {
                if(de.inum == 0)  //inum=0为无效条目，忽略不计
                    continue;
                if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;   
                strcpy(p, de.name);
                find(buf, fname);//进入下一层
                
            }
            break;
        case T_FILE:
            if(strcmp(fmtname(path), fname) == 0)
            {
                write(1, path, strlen(path));
                write(1, "\n", 1);
            }

    }
    close(fd);
    return;

}

int main(int argc, char *argv[])
{
    //假设需要找很多文件，依次把文件名送入find查找函数
    int i;
    for(i=2; i<argc; ++i)
    {
        find(argv[1], argv[i]);
    }
    exit(0);
}