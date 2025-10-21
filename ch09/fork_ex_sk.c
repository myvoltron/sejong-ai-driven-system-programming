#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    printf("PID=%d, PPID=%d, UID=%d, EUID=%d, GID=%d, EGID=%d\n", getpid(),
           getppid(), getuid(), geteuid(), getgid(), getegid());

    pid_t pid1, pid2, pid3;
    int status;

    // 첫 번째 자식: 정상 종료
    if ((pid1 = fork()) == 0) {
        printf("[Child1] PID=%d exiting with 7\n", getpid());
        exit(7);
    }

    // 두 번째 자식: 비정상 종료
    if ((pid2 = fork()) == 0) {
        printf("[Child2] PID=%d will divide by zero\n", getpid());
        int x = 1 / 0;  // SIGFPE 발생
        exit(0);
    }

    // 부모: 자식 상태 회수
    for (int i = 0; i < 2; i++) {
        pid_t cpid = wait(&status); 
        if (WIFEXITED(status)) {
            printf("[Parent] Child %d exited normally, status=%d\n", cpid,
                   status);
        } else if (WIFSIGNALED(status)) {
            printf("[Parent] Child %d terminated by signal %d\n", cpid, status);
        }
    }

    // 세 번째 자식: 손주 생성
    if ((pid3 = fork()) == 0) {
        printf("[Child3] PID=%d will create grandchild\n", getpid());
        if ((pid3 = fork()) == 0) {
            sleep(3);
            printf("[Grandchild] PID=%d says: Hello from grandchild!\n",
                   getpid());
            exit(0);
        } else if (pid3 > 0) {
            exit(0);
        }
    }
    // 부모는 세 번째 자식만 wait
    if (waitpid(pid3, NULL, 0) == pid3) {
        printf("[Parent] Child3 %d finished\n", pid3);
    }

    return 0;
}
