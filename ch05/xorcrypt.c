#include "apue.h"
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <key> <input> <output>\n", argv[0]); 
        exit(1);
    }
    unsigned char key = (unsigned char)argv[1][0]; // 첫 문자를 키로 사용
    int fdin, fdout;
    char buf[4096]; // 읽기/쓰기 버퍼
    ssize_t n;

    // 입력 파일 열기
    if ((fdin = open(argv[2], O_RDONLY)) < 0)
        err_sys("Failed to open input file");

    // 출력 파일 열기(없으면 생성, 있으면 덮어쓰기)
    if ((fdout = open(argv[3], O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
        err_sys("Failed to open output file");

    // 파일을 블록 단위로 읽어서 XOR 변환 후 쓰기
    while ((n = read(fdin, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++)
            buf[i] ^= key;   // XOR 변환
        if (write(fdout, buf, n) != n)
            err_sys("Write failed");
    }
    // 읽기 중 오류 검사
    if (n < 0)
        err_sys("Read failed");

    // 파일 디스크립터 닫기
    close(fdin);
    close(fdout);

    printf("File transformed: %s -> %s (key=%c)\n", argv[2], argv[3], key);
    return 0;
}


// echo "Hello Security!" > plain.txt
// ./xorcrypt K plain.txt enc.txt
// ./xorcrypt K enc.txt dec.txt
// diff plain.txt dec.txt   # 차이가 없어야 성공
