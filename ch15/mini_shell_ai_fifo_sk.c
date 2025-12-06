#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>

int main(void) {
    // FIFO 이름
    const char *fifo_to_child = "to_child_fifo";
    const char *fifo_from_child = "from_child_fifo";

    // FIFO 생성 (이미 존재하면 에러 무시), 모드는 0666
    // your code here
    // your code here
    if (mkfifo(fifo_to_child, 0666) == -1) {
        // 이미 존재하는 경우 에러 무시
    }
    if (mkfifo(fifo_from_child, 0666) == -1) {
        // 이미 존재하는 경우 에러 무시
    }

    // 자식 프로세스 생성
    pid_t pid = fork(); 

    // 자식 프로세스. pid 체크 해야겠죠?
    if (pid == 0) {
        //------------------------------------------------------------------
        // 자식 프로세스
        //------------------------------------------------------------------

        // FIFO 열기 (부모 → 자식 : 자식은 READ)
        // your code here
        int to_child_fd = open(fifo_to_child, O_RDONLY);
        // FIFO 열기 (자식 → 부모 : 자식은 WRITE)
        // your code here// your code here
        int from_child_fd = open(fifo_from_child, O_WRONLY);

        // dup2: 자식 표준입력/출력을 FIFO로 연결
        dup2(to_child_fd, STDIN_FILENO);
        dup2(from_child_fd, STDOUT_FILENO);

        // 사용하지 않는 FIFO 끝 닫기
        // your code here
        // your code here
        close(to_child_fd);
        close(from_child_fd);

        // ai_helper_chat 실행. 
        // your code here
        execl("./ai_helper_chat_repl", "ai_helper_chat_repl", (char *)NULL);

        perror("execl");
        _exit(127);
    }

    //----------------------------------------------------------------------
    // 부모 프로세스
    //----------------------------------------------------------------------

    // 부모 → 자식 : 부모는 WRITE
    int to_child_fd = open(fifo_to_child, O_WRONLY);
    // 자식 → 부모 : 부모는 READ
    int from_child_fd = open(fifo_from_child, O_RDONLY);

    char buf[1024];

    // fd --> FILE 로 변환 하기 위해서 fdopen() 사용. fgets() 사용하려고.
    FILE *fp = fdopen(from_child_fd, "r");

    while (1) {
        printf("mini-shell> ");
        fflush(stdout);

        // 한 줄씩만 입력 받는걸로 하죠. 
        if (!fgets(buf, sizeof(buf), stdin)) 
            break;

        if (strncmp(buf, "exit", 4) == 0)
            break;

        // 자식에게 전달
        // your code here
        write(to_child_fd, buf, strlen(buf));
       
        printf("[AI] ");
        // 자식의 응답 읽기
        // 한 줄씩 읽어서 <<<END>>> 나오면 종료. ai_helper_chat_repl.c에서 끝을 알기 위해서 태그 붙여서 보냄. 
        while (fgets(buf, sizeof(buf), fp)) {
            // <<<END>>> 나오면 응답 끝
            if (strncmp(buf, "<<<END>>>", 9) == 0)
                break;
            printf("%s", buf);
        }
    }

    // FIFO을 fopen()으로 열었어니 fclose() 사용. 
    fclose(fp);
    
    // FIFO 닫기 
    close(to_child_fd);
    close(from_child_fd);

    // 자식 프로세스 대기
    // your code here
    waitpid(pid, NULL, 0);

    return 0;
}
