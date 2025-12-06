#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAXLINE 1024
#define MAXARGS 64

// --- ANSI 색상 코드 ---
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[1;31m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_BLUE "\033[1;34m"
#define COLOR_MAGENTA "\033[1;35m"
#define COLOR_CYAN "\033[1;36m"
#define COLOR_WHITE "\033[1;37m"
#define COLOR_GRAY "\033[0;90m"

// --- 전역 상태 ---
static volatile int ai_mode = 0;
static volatile int ai_thinking = 0;

// --- POSIX SHM / semaphore settings (match ai_helper.c) ---
#define SHM_NAME "/ai_shm"
#define SEM_TO_AI "/sem_to_ai"
#define SEM_TO_PARENT "/sem_to_parent"

typedef struct {
    char prompt[4096];
    char response[8192];
} ShmBuf;

// 문자열 파싱
void parse_line(char* line, char** argv) {
    int i = 0;
    char* token = strtok(line, " \t\n");
    while (token != NULL && i < MAXARGS - 1) {
        argv[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[i] = NULL;
}

// --- SIGQUIT : AI 모드 토글 ---
void handle_sigquit(int signo) {
    (void)signo;  // signo 사용 안함. 안쓰면 경고가 뜸.
    const char msg_on[] = "\n" COLOR_CYAN
                          "╔════════════════════╗\n"
                          "║   AI MODE ON 🤖    ║\n"
                          "╚════════════════════╝" COLOR_RESET
                          "\n" COLOR_MAGENTA "AI> " COLOR_RESET;
    const char msg_off[] = "\n" COLOR_YELLOW
                           "╔════════════════════╗\n"
                           "║  AI MODE OFF 💤    ║\n"
                           "╚════════════════════╝" COLOR_RESET "\n" COLOR_GREEN
                           "shell> " COLOR_RESET;
    ai_mode = !ai_mode;
    write(STDOUT_FILENO, ai_mode ? msg_on : msg_off,
          ai_mode ? sizeof(msg_on) - 1 : sizeof(msg_off) - 1);
}

// --- SIGINT (Ctrl+\) : AI reasoning 중단 ---
void handle_sigint(int signo) {
    (void)signo;  // signo 사용 안함. 안쓰면 경고가 뜸.
    const char msg[] =
        "\n" COLOR_RED "⚠️  AI REASONING INTERRUPTED ⚠️" COLOR_RESET "\n";
    if (ai_mode && ai_thinking) {
        ai_thinking = 0;

        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
}

// --- 터미널 모드 제어 ---
void setup_terminal(struct termios* orig) {
    struct termios new_term;
    tcgetattr(STDIN_FILENO, orig);
    new_term = *orig;
    // ICANON은 유지 (라인 편집 가능), ISIG는 켜서 Ctrl+C 등을 시그널로 변환
    new_term.c_lflag |= ISIG;  // 시그널 활성화
    // Ctrl+T를 VQUIT에 매핑 (SIGQUIT 발생) - AI 모드 토글
    new_term.c_cc[VQUIT] = 20;  // Ctrl+T
    // Ctrl+\를 VINTR에 매핑 (SIGINT 발생) - AI thinking 중단
    new_term.c_cc[VINTR] = 28;  // Ctrl+\ (ASCII 28)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_term);
}
void restore_terminal(struct termios* orig) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig);
}

int main(void) {
    char line[MAXLINE];
    char* argv[MAXARGS];
    pid_t pid;
    int status;
    struct termios orig_termios;

    // --- 시그널 핸들러 등록 ---
    signal(SIGTTIN,
           SIG_IGN);  // background 프로세스가 터미널에서 읽으려고 하면, 무시
    signal(SIGTTOU,
           SIG_IGN);  // background 프로세스가 터미널에 쓰려고 하면, 무시
    signal(SIGTSTP, SIG_IGN);         // Ctrl+Z 무시 (쉘은 멈추면 안됨)
    signal(SIGQUIT, handle_sigquit);  // Ctrl+T
    signal(SIGINT, handle_sigint);    // Ctrl+backslash

    setup_terminal(&orig_termios);

    // --- Create/open shared memory and semaphores, then fork ai_helper ---
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }

    if (ftruncate(shm_fd, sizeof(ShmBuf)) < 0) {
        perror("ftruncate");
        close(shm_fd);
        return 1;
    }

    ShmBuf* shm = mmap(NULL, sizeof(ShmBuf), PROT_READ | PROT_WRITE, MAP_SHARED,
                       shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        return 1;
    }

    sem_t* sem_to_ai = sem_open(SEM_TO_AI, O_CREAT, 0666, 0);
    if (sem_to_ai == SEM_FAILED) {
        perror("sem_open to_ai");
        return 1;
    }
    sem_t* sem_to_parent = sem_open(SEM_TO_PARENT, O_CREAT, 0666, 0);
    if (sem_to_parent == SEM_FAILED) {
        perror("sem_open to_parent");
        return 1;
    }

    // Fork ai_helper child if shared memory and semaphores were created
    pid_t ai_child = fork();
    if (ai_child == 0) {
        // Crtl + T로 AI 모드 토글 시, ai_helper가 종료되지 않도록 별도 프로세스 그룹으로 분리
        setpgid(0, 0);
        // Child: exec the ai_helper program (assumes ./ai_helper exists)
        execl("./ai_helper", "ai_helper", (char*)NULL);
        // If execl fails, print and exit child
        perror("execl ai_helper");
        _exit(127);
    } else if (ai_child < 0) {
        perror("fork");
        return 1;
    }

    // line buffered
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf(COLOR_CYAN
           "╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                     🚀 AI Assist Shell 🤖                 ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  " COLOR_YELLOW "Ctrl+T" COLOR_CYAN
           "  : Toggle AI Mode                                 ║\n");
    printf("║  " COLOR_YELLOW "Ctrl+\\" COLOR_CYAN
           "  : Stop AI Thinking                               ║\n");
    printf("║  " COLOR_YELLOW "Ctrl+D" COLOR_CYAN
           "  : Exit Shell                                     ║\n");
    printf(
        "╚═══════════════════════════════════════════════════════════"
        "╝" COLOR_RESET "\n");
    printf(COLOR_GREEN "shell> " COLOR_RESET);
    fflush(stdout);

    while (1) {
        // fgets로 라인 단위 입력 (편집 가능!)
        if (fgets(line, MAXLINE, stdin) == NULL) {
            break;  // EOF (Ctrl+D)
        }

        // 빈 줄 처리
        if (line[0] == '\n') {
            printf(ai_mode ? COLOR_MAGENTA "AI> " COLOR_RESET
                           : COLOR_GREEN "shell> " COLOR_RESET);
            fflush(stdout);
            continue;
        }

        /* AI 모드 */
        if (ai_mode) {
            size_t len = strlen(line);
            if (len && line[len - 1] == '\n') line[len - 1] = '\0';

            strncpy(shm->prompt, line, sizeof(shm->prompt) - 1);
            shm->prompt[sizeof(shm->prompt) - 1] = '\0';

            sem_post(sem_to_ai);
            ai_thinking = 1;

            printf(COLOR_GRAY "[AI] Waiting for response..." COLOR_RESET "\n");

            struct timespec ts;
            if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
                perror("clock_gettime");
            }
            ts.tv_sec += 120; /* 2 minutes */
            int s = sem_timedwait(sem_to_parent, &ts);
            if (s == -1) {
                if (errno == ETIMEDOUT) {
                    fprintf(stderr, COLOR_YELLOW
                            "[AI] response timed out after 2 "
                            "minutes\n" COLOR_RESET);
                } else {
                    perror("sem_timedwait to_parent");
                }
            } else {
                printf(COLOR_CYAN "[AI] " COLOR_RESET "%s\n", shm->response);
            }

            ai_thinking = 0;
            printf(COLOR_MAGENTA "AI> " COLOR_RESET);
            fflush(stdout);
            continue;
        }

        /* 비-AI 명령은 토크나이즈 후 처리 */
        parse_line(line, argv);
        if (argv[0] == NULL) {
            printf(COLOR_GREEN "shell> " COLOR_RESET);
            fflush(stdout);
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) break;

        // --- 내부 명령어: cd ---
        if (strcmp(argv[0], "cd") == 0) {
            if (argv[1] == NULL)
                fprintf(stderr,
                        COLOR_RED "cd: missing argument" COLOR_RESET "\n");
            else if (chdir(argv[1]) != 0)
                perror("cd");
            printf(COLOR_GREEN "shell> " COLOR_RESET);
            fflush(stdout);
            continue;
        }

        // --- 외부 명령 실행 ---
        pid = fork();
        if (pid < 0) {
            perror("fork");
        } else if (pid == 0) {
            // 자식: 원래 터미널 모드로 복원
            restore_terminal(&orig_termios);
            setpgid(0, 0);
            execvp(argv[0], argv);
            perror("execvp");
            exit(127);
        } else {
            // 부모: 원래 터미널 모드로 복원
            restore_terminal(&orig_termios);
            setpgid(pid, pid);
            tcsetpgrp(STDIN_FILENO, pid);

            if (waitpid(pid, &status, WUNTRACED) < 0) perror("waitpid");

            // 복귀 후 다시 설정
            tcsetpgrp(STDIN_FILENO, getpgrp());
            setup_terminal(&orig_termios);
        }

        printf(COLOR_GREEN "shell> " COLOR_RESET);
        fflush(stdout);
        continue;
    }

    restore_terminal(&orig_termios);
    printf(COLOR_CYAN "\n👋 Goodbye!\n" COLOR_RESET);

    // --- Cleanup: notify AI helper to exit, wait, and release resources ---
    if (shm != MAP_FAILED && sem_to_ai != SEM_FAILED &&
        sem_to_parent != SEM_FAILED) {
        // send exit command to AI helper
        strncpy(shm->prompt, "exit", sizeof(shm->prompt) - 1);
        shm->prompt[sizeof(shm->prompt) - 1] = '\0';
        sem_post(sem_to_ai);

        if (ai_child > 0) waitpid(ai_child, NULL, 0);

        munmap(shm, sizeof(ShmBuf));
    }

    if (shm_fd >= 0) close(shm_fd);
    if (sem_to_ai != SEM_FAILED) {
        sem_close(sem_to_ai);
        sem_unlink(SEM_TO_AI);
    }
    if (sem_to_parent != SEM_FAILED) {
        sem_close(sem_to_parent);
        sem_unlink(SEM_TO_PARENT);
    }
    if (shm_fd >= 0) shm_unlink(SHM_NAME);

    return 0;
}
