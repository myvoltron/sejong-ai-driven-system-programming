#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

jmp_buf env;

void process_line(char *line)
{
    line[strcspn(line, "\n")] = 0; // 개행 제거

    if (strncmp(line, "print ", 6) == 0)
    {
        // your code here (문자열 출력)
        printf("%s\n", line + 6);
    }
    else if (strncmp(line, "set ", 4) == 0)
    {
        char *str = NULL;
        char *kv = line + 4;
        if (strchr(kv, '=') == NULL)
        {
            // 잘못된 형식
            fprintf(stderr, "잘못된 형식입니다.\n");
            longjmp(env, 1);
        }

        // your code here (malloc으로 문자열 공간 확보)
        str = malloc(strlen(kv) + 1);
        if (str == NULL)
        {
            fprintf(stderr, "메모리 할당 실패\n");
            longjmp(env, 1);
        }
        // your code here (strncpy로 전체 복사)
        strncpy(str, kv, strlen(kv) + 1);

        // your code here (putenv 호출)
        if (putenv(str) != 0)
        {
            fprintf(stderr, "환경 변수 설정 실패\n");
            longjmp(env, 1);
        }

        // your code here (성공 메시지 출력)
        printf("환경변수 등록: %s\n", str);
    }
    else if (strcmp(line, "exit") == 0)
    {
        // your code here (종료)
        printf("종료합니다.\n");
        exit(0);
    }
    else
    {
        // your code here (getenv로 조회, 없으면 longjmp)
        char *value = getenv(line);
        if (value == NULL)
        {
            fprintf(stderr, "환경 변수 '%s'가 설정되어 있지 않습니다.\n", line);
            longjmp(env, 1);
        }
        printf("%s\n", value);
    }
}

int main(void)
{
    char line[128];

    while (1)
    {
        if (setjmp(env) != 0)
            printf("에러 발생 → REPL 복구 완료\n");

        printf(">>> ");
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;
        process_line(line);
    }
    return 0;
}
