#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#define BUFFSIZE 4096

void cat(int fd) {
    char buf[PATH_MAX];
    ssize_t n;

    while ((n = read(fd, buf, PATH_MAX)) > 0) {
        if (write(STDOUT_FILENO, buf, n) != n) {
            perror("write error");
            exit(1);
        }
    }
    if (n < 0) {
        perror("read error");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    int fd;

    if (argc == 1) {
        // 인자가 없으면 표준 입력 사용
        cat(STDIN_FILENO);
    } else {
        for (int i = 1; i < argc; i++) {
            fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                perror(argv[i]);
                continue;   // 에러 발생해도 다음 파일로 진행
            }
            cat(fd);
            close(fd);
        }
    }
    return 0;
}
