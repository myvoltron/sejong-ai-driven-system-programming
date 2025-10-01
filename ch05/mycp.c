#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#define BUFFSIZE 4096

int main(void) {
    int original_file, copied_file;
    int nread;
    char buf[PATH_MAX];

    original_file = open("source.txt", O_RDONLY);
    if (original_file < 0) {
        perror("open directory");
        return 1;
    }

    /* 없는 directory는 만들어주지 않음. */
    copied_file = open("test/dest2.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (copied_file < 0) {
        perror("open copied");
        return 1;
    }

    while((nread = read(original_file, buf, PATH_MAX)) > 0) {
        if (write(copied_file, buf, nread) != nread) {
            perror("write");
            close(original_file);
            close(copied_file);
            exit(1);
        }
    }
    if (nread < 0) perror("read");

    close(original_file);
    close(copied_file);
    return 0;
}

// exit와 return 으로 종료하는 것의 차이점?