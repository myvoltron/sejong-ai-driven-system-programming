#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define BUFF 4096

void mkdirs(char *dir_path)
{
    char buff[BUFF];
    char *p_dir = buff;

    strcpy(buff, dir_path);

    buff[BUFF - 1] = '\0';

    while (*p_dir)
    {
        if ('/' == *p_dir)
        {
            *p_dir = '\0';
            mkdir(buff, 0777);
            *p_dir = '/';
        }
        p_dir++;
    }
}
int main(int argc, char *argv[])
{
    // mkdirs("baboo/");
    if (mkdir("baboo/", 0777) != 0)
    {
        perror("mkdir");
    }
    if (mkdir("test1/test2/test7.txt", 0777) != 0)
    {
        perror("mkdir");
    }
    mkdirs("asdf/asdf2/asdf4.txt");
    // if (mkdir("test1/test2/test3", 0777) != 0)
    // {
    //     perror("mkdir");
    // }
    // if (mkdir("./test2", 0777) != 0) {
    //     perror("mkdir");
    // }
    return 0;
}