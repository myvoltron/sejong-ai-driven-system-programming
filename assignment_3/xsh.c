#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ANSI color codes used for prompt and messages */
#define ANSI_GREEN "\x1b[1;32m"
#define ANSI_BLUE "\x1b[1;34m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_RESET "\x1b[0m"

#define MAXLINE 1024
#define MAX_ARGS 64
#define MAX_REDIR 8

typedef enum {
    REDIRECTION_INPUT,  /* < */
    REDIRECTION_OUTPUT, /* > */
    REDIRECTION_APPEND, /* >> */
} RedirectionType;

typedef struct {
    int fd;               /* 0=stdin,1=stdout,2=stderr */
    char* filename;       /* target filename (points into line buffer) */
    RedirectionType type; /* redirection type */
} RedirectionItem;

int redirect_fds(RedirectionItem* r, int n);
void parse_and_exec(char** tokens);
void parse_line(char* line, char** argv);
char* get_cwd_basename(void);

int main(void) {
    char line[MAXLINE];
    char* argv[MAX_ARGS];
    pid_t shell_pid = getpid();

    // 해당 시그널을 무시
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    /* sleep_print.sh를 백그라운드로 실행했을때 제어권을 잃는 경우가 생김. 이를
     * 위해 SIGCHLD 핸들러를 등록해서 자식을 회수하는 작업이 필요함. */
    /* 하지만 일단 과제 범위를 벗어난다고 판단하고 구현은 하지 않음. */

    while (1) {
        char* base = get_cwd_basename();
        if (base == NULL) {
            perror("getcwd");
            exit(1);
        }
        printf(ANSI_GREEN "xsh[%d]" ANSI_RESET ":" ANSI_BLUE "%s" ANSI_RESET
                          "> ",
               (int)shell_pid, base);
        if (base != NULL) {
            free(base);
        }
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;  // EOF (Ctrl+D)
        }
        if (line[0] == '\n') continue;

        parse_line(line, argv);

        /* handle the parsed tokens: build argv, redirs, background flag and
         * execute */
        parse_and_exec(argv);
    }

    return 0;
}
/*
 * 함수 원형: 현재 작업 디렉터리 경로에서 마지막 부분(베이스 이름)을 반환합니다.
 * 반환값: malloc으로 할당된 문자열(성공) 또는 NULL(오류).
 * 호출자는 반환값이 NULL이 아닌 경우 free()로 해제해야 합니다.
 *
 * 구현: getcwd로 현재 경로를 얻어 마지막 '/' 이후의 부분을 추출하여
 * 새로 할당된 문자열로 반환합니다. 루트("/")의 경우 "/"을 반환합니다.
 */
char* get_cwd_basename(void) {
    char cwd[MAXLINE];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }
    /* find last '/' */
    char* base = strrchr(cwd, '/');
    const char* res;
    if (base) {
        if (*(base + 1) == '\0') {
            res = "/";
        } else {
            res = base + 1;
        }
    } else {
        res = cwd;
    }
    size_t len = strlen(res) + 1;
    char* out = malloc(len);
    if (!out) {
        return NULL;
    }
    memcpy(out, res, len);
    return out;
}
/* 문자열을 공백 기준으로 나누어 argv 배열 생성 */
void parse_line(char* line, char** argv) {
    int i = 0;
    char* token = strtok(line, " \t\n");
    while (token != NULL && i < MAX_ARGS - 1) {
        argv[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[i] = NULL;
}
/* 각 리다이렉션 항목에 대해 파일 디스크립터를 설정합니다. */
int redirect_fds(RedirectionItem* r, int n) {
    for (int i = 0; i < n; ++i) {
        int flags = 0;
        if (r[i].type == REDIRECTION_INPUT) {
            flags = O_RDONLY;
        } else if (r[i].type == REDIRECTION_OUTPUT) {
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        } else if (r[i].type == REDIRECTION_APPEND) {
            flags = O_WRONLY | O_CREAT | O_APPEND;
        }

        int fd = open(r[i].filename, flags, 0644);
        if (fd < 0) {
            perror("redirect_fds > open");
            return -1;
        }
        /* redirection의 핵심 */
        /* dup2 함수를 통해 stdin, stdout 둘 중 하나가 fd의 file table entry를
         * 가리키게 됨. */
        if (dup2(fd, r[i].fd) < 0) {
            perror("redirect_fds > dup2");
            close(fd);
            return -1;
        }
        /* stdin, stdout 둘 중 하나가 fd의 file table entry를 가리키므로 fd는 더
         * 이상 필요하지 않음. */
        close(fd);
    }
    return 0;
}
void parse_and_exec(char** tokens) {
    RedirectionItem redir_items[MAX_REDIR];
    char* argv[MAX_ARGS];

    int argv_count = 0;
    int redir_count = 0;
    bool is_background = false;

    pid_t child_pid;
    int status;

    for (int i = 0; tokens[i] != NULL; ++i) {
        char* t = tokens[i];
        if (strcmp(t, "<") == 0) {
            if (tokens[i + 1] && redir_count < MAX_REDIR) {
                redir_items[redir_count].fd = STDIN_FILENO;
                redir_items[redir_count].filename = tokens[i + 1];
                redir_items[redir_count].type = REDIRECTION_INPUT;
                redir_count++;
                i++;
            }
        } else if (strcmp(t, ">>") == 0) {
            if (tokens[i + 1] && redir_count < MAX_REDIR) {
                redir_items[redir_count].fd = STDOUT_FILENO;
                redir_items[redir_count].filename = tokens[i + 1];
                redir_items[redir_count].type = REDIRECTION_APPEND;
                redir_count++;
                i++;
            }
        } else if (strcmp(t, ">") == 0) {
            if (tokens[i + 1] && redir_count < MAX_REDIR) {
                redir_items[redir_count].fd = STDOUT_FILENO;
                redir_items[redir_count].filename = tokens[i + 1];
                redir_items[redir_count].type = REDIRECTION_OUTPUT;
                redir_count++;
                i++;
            }
        } else if (strcmp(t, "&") == 0) {
            is_background = true;
        } else {
            if (t[0] == '$' && t[1] != '\0') {
                /* 환경변수 치환 */
                char* val = getenv(t + 1);
                argv[argv_count++] = val ? val : "";
            } else {
                argv[argv_count++] = t;
            }
        }
        /* 명령어 인자 수가 최대치를 초과하면 중단 */
        if (argv_count >= MAX_ARGS - 1) {
            break;
        }
    }
    argv[argv_count] = NULL;

    /* empty command */
    if (argv[0] == NULL) {
        return;
    }

    /* builtins handled in parent */
    if (strcmp(argv[0], "exit") == 0) exit(0);
    if (strcmp(argv[0], "cd") == 0) {
        if (argv[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
        } else if (chdir(argv[1]) != 0) {
            perror("cd");
        }
        return;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        char cwd[MAXLINE];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd");
        } else {
            printf("%s\n", cwd);
        }
        return;
    }

    /* fork a child process */
    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        return;
    } else if (child_pid == 0) {
        setpgid(0, 0);
        if (redir_count > 0) {
            if (redirect_fds(redir_items, redir_count) < 0) {
                fprintf(stderr, "xsh: failed to redirect file descriptors\n");
                exit(1);
            }
        }
        execvp(argv[0], argv);
        fprintf(stderr, "xsh: command not found: %s\n", argv[0]);
        exit(127);
    } else {
        setpgid(child_pid, child_pid);
        if (is_background) {
            /* 백그라운드 작업시 부모 프로세스는 대기하지 않음 */
            printf(ANSI_YELLOW "[bg] started pid=%d" ANSI_RESET "\n",
                   (int)child_pid);
            fflush(stdout);
            return;
        } else {
            tcsetpgrp(STDIN_FILENO, child_pid);
            if (waitpid(child_pid, &status, WUNTRACED) < 0) {
                perror("waitpid");
            }
            tcsetpgrp(STDIN_FILENO, getpgrp());
            return;
        }
    }
}