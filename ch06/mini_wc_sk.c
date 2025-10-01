#include <stdio.h>
#include <stdlib.h>

#define BUFFSIZE 4096

int main(int argc, char *argv[]) {    
    int line_count = 0, word_count = 0;
    long filesize = 0;
    int offset;
    char ch;

    char buf[PATH_MAX];

    FILE *fp = NULL;
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 2;
    }
    fp = fopen(argv[1], "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    // 라인 단위 읽기 (fgets)
    // your code here
    while(fgets(buf, PATH_MAX, fp) != NULL) {
        line_count += 1;
    }
    
    
    rewind(fp); // 파일 처음으로 이동    

    // 파일 길이 구하기 (fseek, ftell))
    // your code here 
    offset = ftell(fp);
    offset = fseek(fp, 0, SEEK_END) - offset; 
    filesize = offset;

    rewind(fp);

    // 문자/단어 단위 읽기 (fgetc)
    // your code here
    while(1) {
        ch = fgetc(fp);
        if (ch == ' ') {
            word_count += 1;
        }
        if (ch == NULL) {
            break;
        }
    }

    printf("Line count : %d\n", line_count);
    printf("Word count : %d\n", word_count);
    printf("File size : %ld \n", filesize);

    fclose(fp);
    return 0;
}
