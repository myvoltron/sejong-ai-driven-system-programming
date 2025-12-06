// mini_shell_ai_shm.c

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <stdlib.h>

#define SHM_NAME "/ai_shm"
#define SEM_TO_AI "/sem_to_ai"         // 부모 → AI
#define SEM_TO_PARENT "/sem_to_parent" // AI → 부모

typedef struct {
    char prompt[4096];
    char response[8192];
} ShmBuf;

int main(void) {
    // 공유 메모리 생성
    int shm_fd = // your code here
    // 공유 메모리 열기 실패시 오류 처리
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }

    // 공유 메모리 크기 설정 ftruncate 사용, 에러처리도 같이 해주세요. 
    if ( // your code here ) < 0) {
        perror("ftruncate");
        close(shm_fd);
        return 1;
    }

    // 공유 메모리 매핑, MAP_SHARED, PROT_READ | PROT_WRITE 사용
    ShmBuf *shm = // your code here

    // 매핑 실패시 오류 처리
    if (shm == // your code here ) {
        perror("mmap");
        close(shm_fd);
        return 1;
    }

    // 세마포어 생성 (부모가 O_CREAT)
    sem_t *sem_to_child = // your code here
    // 세마포어 생성 실패시 오류 처리
    if (sem_to_child == // your code here ) {
        perror("sem_open to_ai");
        return 1;
    }

    // 세마포어 생성 (부모가 O_CREAT)
    sem_t *sem_to_parent = // your code here;
    if (sem_to_parent == // your code here ) {
        perror("sem_open to_parent");
        return 1;
    }

    // 자식 프로세스 생성 (AI 프로세스 실행)
    pid_t pid = fork();

    if (pid == 0) {
        // 자식: ai_helper_chat_repl_shm_posix 실행 (아래에서 수정한 버전)
        execl("./ai_helper_chat_repl_shm_posix", "ai_helper_chat_repl_shm_posix", (char *)NULL);
        perror("execl");
        _exit(127);
    }

    // 부모: mini-shell 역할
    char buf[1024];

    // line buffered
    setvbuf(stdout, NULL, _IOLBF, 0);

    while (1) {
        printf("mini-shell> ");
        fflush(stdout);

        if (!fgets(buf, sizeof(buf), stdin))
            break;

        if (strncmp(buf, "exit", 4) == 0)
            break;

        // 개행 제거
        size_t len = strlen(buf);
        if (len && buf[len-1] == '\n')
            buf[len-1] = '\0';

        // 공유 메모리에 질문 쓰기. 편하지 않습니까? strncpy 사용하고, 마지막에 null 도 넣어주세요. 
        // your code here
        // your code here

        // AI에게 알림(세마포어 이용)
        // your code here

        // AI 응답 대기(세마포어 이용)
        // your code here

        // 응답 출력
        printf("[AI] %s\n", shm->response);
    }

    // 종료 처리
    // AI에게도 종료 신호 보내고 싶으면 필요시 여기서 사용:
    strncpy(shm->prompt, "exit", sizeof(shm->prompt));
    // AI에게 알림(세마포어 이용)
    sem_post(sem_to_child); 

    // 자식 프로세스 대기
    waitpid(pid, NULL, 0);

    // 공유 메모리 및 세마포어 정리
    // your code here
    // your code here

    // 세마포어 닫기
    // your code here
    // your code here

    // 세마포어 및 공유 메모리 제거(sem_unlink, shm_unlink)
    // your code here
    // your code here
    // your code here

    return 0;
}
