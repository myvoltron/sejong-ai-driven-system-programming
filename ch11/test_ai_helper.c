#include "ai_helper.c"
#include <stdio.h>

int main() {
    char response[4096];

    printf("AI helper test starting...\n\n");

    printf("질문: 안녕하세요?\n");
    if (get_ai_summary("안녕하세요?", response, sizeof(response)) == 0) {
        printf("답변: %s\n\n", response);
    } else {
        printf("error occured!\n\n");
    }

    printf("질문: 시스템 프로그래밍에 대해서 설명해주세요\n");
    if (get_ai_summary("시스템 프로그래밍에 대해서 설명해주세요", response, sizeof(response)) == 0) {
        printf("답변: %s\n\n", response);
    } else {
        printf("error occured!\n\n");
    }

    return 0;
}