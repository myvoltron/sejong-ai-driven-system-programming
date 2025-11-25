#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    // 명령행 인자 확인: FIFO 경로 필요
    if (argc != 2) {
        fprintf(stderr, "usage: %s fifo_path\n", argv[0]);
        return 1;
    }

    // FIFO를 논블로킹 읽기 모드로 열기
    // your code here
    int fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return 1; }

    // poll용 파일 디스크립터 배열 설정 2개 
    // your code here
    struct pollfd fds[2];
    
    // 표준입력을 위한 설정
    // your code here
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;

    // FIFO를 위한 설정
    // your code here
    fds[1].fd = fd;
    fds[1].events = POLLIN;

    char buf[1024];

    while (1) {
        // 둘 중 하나라도 읽을 데이터가 생길 때까지 대기 (무한 타임아웃)
        // your code here
        if (poll(fds, 2, -1) < 0) {
            perror("poll");
            break;
        }

        // 표준 입력에 데이터 도착. 어떤 이벤트가 발생했는지 확인(힌트 : AND 연산자)
        if (fds[0].revents & POLLIN) { 
            int r = read(STDIN_FILENO, buf, sizeof(buf));
            if (r == 0) {
                // EOF 처리: 표준 입력 종료 시 프로그램 종료
                printf("STDIN EOF, exiting...\n");
                break;
            }
            write(STDOUT_FILENO, "[STDIN] ", 8); // 접두사 출력 
            write(STDOUT_FILENO, buf, r); // 입력 데이터 출력
        }

        // FIFO에서 데이터 도착
        if (fds[1].revents & POLLIN) {
            int r = read(fd, buf, sizeof(buf));
            if (r > 0) {
                write(STDOUT_FILENO, "[FIFO] ", 7);
                write(STDOUT_FILENO, buf, r); // FIFO 데이터 출력     
            }
        }
    }
}

// poll() 실습: STDIN + FIFO 동시 감시
//
// 터미널 1:
//   $ mkfifo myfifo
//   $ echo "hello" > myfifo
//
// 터미널 2:
//   $ ./poll_test myfifo
//   - 키보드 입력  → [STDIN]
//   - FIFO 입력    → [FIFO]
//   - FIFO 종료    → [FIFO EOF]
