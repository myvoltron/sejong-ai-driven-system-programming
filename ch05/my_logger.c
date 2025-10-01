#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define BUFFSIZE 4096

int main(int argc, char *argv[]) {
    int fd;
    char buf[PATH_MAX];
    ssize_t n; 

    if (argc != 2) {
        fprintf("usage: %s <message>", argv[0]);
    } 

    if ((fd = open("minilog.txt", O_WRONLY | O_CREAT | O_APPEND, 0600)) < 0) {
        perror("open error for minilog.txt");
    }

    n = snprintf(buf, sizeof(buf), "user:%d msg:%s\n", getuid(), argv[1]);

    if (write(fd, buf, n) < 0)
        perror("write error");
     
    close(fd);
    exit(0);
}