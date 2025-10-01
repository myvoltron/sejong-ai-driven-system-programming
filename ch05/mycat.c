#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

#define BUFFSIZE 4096

int main(void) {
    int original_file, copied_file;
    char buf[PATH_MAX];

    original_file = open("source.bin", O_RDONLY);
    if (original_file < 0) {
        perror("open directory");
        return 1;
    }

    copied_file = open("dest.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (copied_file < 0) {
        perror("open copied");
        return 1;
    }

    while(read(original_file, buf, PATH_MAX) != 0) {
        write(copied_file, buf, PATH_MAX);
    }

    close(original_file);
    close(copied_file);
    return 0;
}

// exit와 return 으로 종료하는 것의 차이점?