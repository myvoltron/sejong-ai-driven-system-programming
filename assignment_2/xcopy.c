#include "apue.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BUFFSIZE 4096
#define DEFAULT_DIRECTORY_MODE 0777
#define DEFAULT_FILE_MODE 0644

extern int optind;

bool recursive_opt = false, verbose_opt = false, preserve_opt = false;

static void print_usage(const char *argv0) {
    fprintf(stderr, "Usage: %s [-r] [-v] [-p] SOURCE TARGET\n", argv0);
}
static void print_skip_symlink_message(const char *path) {
    fprintf(stderr, "skip symlinks: %s\n", path);
}
static void print_verbose_message(const char *src, const char *dest) {
    printf("복사 중: %s -> %s\n", src, dest);
}

int copy_entry(const char *src, const char *dest);
int copy_directory_recursive(const char *src, const char *dest);
int copy_file(const char *src, const char *dest);
int safe_mkdir(const char *path, mode_t mode);
int mkdirs(const char *dir_path);
int preserve_file_attributes(const char *src, const char *dest);
int process_preserve(const char *src, const char *dest);
bool file_exists(const char *path);
char *concat_path(const char *src, const char *name);
mode_t get_permission_bits(mode_t st_mode);

int main(int argc, char *argv[]) {
    int opt;

    // optstring: r, v, p 모두 인자 없음
    // 앞에 '+'를 붙이면 첫 비옵션에서 파싱을 멈추는 POSIX 모드
    while ((opt = getopt(argc, argv, "+rvp")) != -1) {
        switch (opt) {
            case 'r':
                recursive_opt = true;
                break;
            case 'v':
                verbose_opt = true;
                break;
            case 'p':
                preserve_opt = true;
                break;
            case '?': /* 미지정 옵션 */
            default:
                print_usage(argv[0]);
                return 2;
        }
    }

    // 남은 비옵션 인자: SOURCE, DEST
    if (argc - optind != 2) {
        print_usage(argv[0]);
        return 2;
    }
    const char *src = argv[optind];
    const char *dest = argv[optind + 1];

    if (copy_entry(src, dest) != 0) {
        fprintf(stderr, "copy failed\n");
        return 1;
    }
    return 0;
}

// 옵션 그리고 src와 dest에 따라서 분기 처리 후 복사합니다.
// with -r option, 디렉터리 복사 모드
// without -r option, 단일 파일 복사 모드
int copy_entry(const char *src, const char *dest) {
    struct stat st_src;

    if (lstat(src, &st_src) != 0) {
        perror("lstat src");
        return -1;
    }

    if (S_ISLNK(st_src.st_mode)) {
        print_skip_symlink_message(src);
    } else if (S_ISDIR(st_src.st_mode)) {
        struct stat st_dst;

        if (!recursive_opt) {
            fprintf(
                stderr,
                "without -r, SOURCE must be a file path (not a directory)\n");
            return -1;
        }
        /* 이후 코드들은 recursive_opt == true일 때만 실행됨. */

        /* 디렉터리 복사 시 SOURCE와 TARGET 모두 디렉터리임이 보장되어야함. (TARGET은 없어도 됨.) */
        if (lstat(dest, &st_dst) != 0) {
            /* dest directory가 없다면 생성 */
            if (mkdirs(dest) != 0) {
                fprintf(stderr, "mkdirs error: %s\n", dest);
                return -1;
            }

            // src가 디렉터리이고 dest가 없을 때 재귀적으로 복사
            if (copy_directory_recursive(src, dest) != 0) {
                fprintf(stderr, "copy_directory_recursive error: %s -> %s\n",
                        src, dest);
                return -1;
            }
        } else if (S_ISLNK(st_dst.st_mode)) {
            /* skip symlinks */
            print_skip_symlink_message(dest);
        } else if (S_ISREG(st_dst.st_mode)) {
            fprintf(stderr,
                    "with -r, TARGET must be a directory (not a file)\n");
            return -1;
        } else if (S_ISDIR(st_dst.st_mode)) {
            // src와 dest 모두 디렉터리인 경우, 재귀적으로 복사
            if (copy_directory_recursive(src, dest) != 0) {
                fprintf(stderr, "copy_directory_recursive error: %s -> %s\n",
                        src, dest);
                return -1;
            }
        }
        // 그 외 파일 유형들은 무시
    } else if (S_ISREG(st_src.st_mode)) {
        // src가 정규 파일인 경우
        struct stat st_dst;

        if (recursive_opt) {
            fprintf(stderr,
                    "with -r, SOURCE must be a directory (got a file)\n");
            return -1;
        }

        /* 파일 복사 시 SOURCE와 TARGET 모두 파일임이 보장되어야함. */
        if (lstat(dest, &st_dst) != 0) {
            /* TARGET 파일이 없다면 생성하면서 복사 */
            if (copy_file(src, dest) != 0) {
                fprintf(stderr, "copy_file error: %s -> %s\n", src, dest);
                return -1;
            }
        } else if (S_ISLNK(st_dst.st_mode)) {
            /* skip symlinks */
            print_skip_symlink_message(dest);
        } else if (S_ISREG(st_dst.st_mode)) {
            if (copy_file(src, dest) != 0) {
                fprintf(stderr, "copy_file error: %s -> %s\n", src, dest);
                return -1;
            }
        } else if (S_ISDIR(st_dst.st_mode)) {
            fprintf(stderr,
                    "without -r, TARGET must be a file (not a directory)\n");
            return -1;
        } else {
            fprintf(stderr, "unsupported source type\n");
            return -1;
        }
    } else { /* Other types are ignored */
        fprintf(stderr, "unsupported source type\n");
        return -1;
    }

    // -p 옵션이 켜져있다면, 파일 및 디렉터리 복사 이후에 속성까지 복사합니다.
    if (preserve_opt) {
        if (process_preserve(src, dest) != 0) {
            fprintf(stderr, "process_preserve error: %s -> %s\n", src, dest);
            return -1;
        }
    }

    return 0;
}

int copy_directory_recursive(const char *src, const char *dest) {
    DIR *dp;
    struct dirent *dirp;
    struct stat st;

    if (lstat(src, &st) != 0) {
        perror("lstat src");
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "recurv: %s is not a directory\n", src);
        return -1;
    }

    if ((dp = opendir(src)) == NULL) {
        perror("opendir");
        return -1;
    }
    while ((dirp = readdir(dp)) != NULL) {
        struct stat child_st;
        char *child_src, *child_dest;

        if (strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0)
            continue; /* ignore dot and dot-dot */

        child_src = concat_path(src, dirp->d_name);
        child_dest = concat_path(dest, dirp->d_name);
        if (!child_src || !child_dest) {
            perror("alloc_path_concat");
            if (child_src) free(child_src);
            if (child_dest) free(child_dest);
            closedir(dp);
            return -1;
        }

        if (lstat(child_src, &child_st) != 0) {
            perror("lstat");
            free(child_src);
            free(child_dest);
            closedir(dp);
            return -1;
        } else if (S_ISLNK(child_st.st_mode)) {
            print_skip_symlink_message(child_src);
            free(child_src);
            free(child_dest);
            continue;
        } else if (S_ISDIR(child_st.st_mode)) {
            /* ensure directory on dest side */
            if (safe_mkdir(child_dest, DEFAULT_DIRECTORY_MODE) != 0) {
                fprintf(stderr, "safe_mkdir error: %s\n", child_dest);
                free(child_src);
                free(child_dest);
                return -1;
            }

            /* recursive call */
            if (copy_directory_recursive(child_src, child_dest) != 0) {
                free(child_src);
                free(child_dest);
                closedir(dp);
                return -1;
            }
        } else if (S_ISREG(child_st.st_mode)) {
            /* copy file */
            if (copy_file(child_src, child_dest) != 0) {
                free(child_src);
                free(child_dest);
                closedir(dp);
                return -1;
            }
        }
        free(child_src);
        free(child_dest);
    }

    if (closedir(dp) < 0) {
        perror("closedir");
        return -1;
    }
    return 0;
}
int copy_file(const char *src, const char *dest) {
    int n;
    int in, out;
    char *buf[BUFFSIZE];

    if (verbose_opt) {
        print_verbose_message(src, dest);
    }

    in = open(src, O_RDONLY);
    if (in < 0) {
        perror("open src");
        return -1;
    }

    /* Check if destination file exists and print warning */
    if (file_exists(dest)) {
        fprintf(stderr, "Warning: File '%s' already exists.\n", dest);
    }
    out = open(dest, O_WRONLY | O_CREAT | O_TRUNC, DEFAULT_FILE_MODE);
    if (out < 0) {
        fprintf(stderr, "open dest failure: %s\n", dest);
        close(in);
        return -1;
    }

    while ((n = read(in, buf, BUFFSIZE)) > 0) {
        if (write(out, buf, n) != n) {
            perror("write");
            close(in);
            close(out);
            return -1;
        }
    }
    if (n < 0) {
        perror("read");
        close(in);
        close(out);
        return -1;
    }

    if (close(in) < 0) perror("close src");
    if (close(out) < 0) {
        perror("close dst");
        return -1;
    }
    return 0;
}
int preserve_file_attributes(const char *src, const char *dest) {
    struct stat stat_buf;
    if (stat(src, &stat_buf) != 0) {
        perror("stat");
        return -1;
    }

    if (chmod(dest, stat_buf.st_mode) != 0) {
        perror("chmod");
        return -1;
    }

    struct timespec times[2];
    times[0] = stat_buf.st_atim;
    times[1] = stat_buf.st_mtim;
    if (utimensat(AT_FDCWD, dest, times, 0) != 0) {
        perror("utimensat");
        return -1;
    }

    return 0;
}
int process_preserve(const char *src, const char *dest) {
    struct stat st;

    if (lstat(src, &st) != 0) {
        perror("lstat");
        return -1;
    }

    if (S_ISLNK(st.st_mode)) {
        /* preserve시 심볼릭 링크 무시 출력문 스킵 */
        // print_skip_symlink_message(src);
    } else if (S_ISDIR(st.st_mode)) {
        DIR *dp;
        struct dirent *dirp;
        if ((dp = opendir(src)) == NULL) {
            perror("opendir");
            return -1;
        }

        while ((dirp = readdir(dp)) != NULL) {
            struct stat child_st;

            if (strcmp(dirp->d_name, ".") == 0 ||
                strcmp(dirp->d_name, "..") == 0)
                continue;

            // 동적 경로 버퍼 할당 함수 사용
            char *child_src = concat_path(src, dirp->d_name);
            char *child_dest = concat_path(dest, dirp->d_name);
            if (!child_src || !child_dest) {
                perror("alloc_path_concat");
                if (child_src) free(child_src);
                if (child_dest) free(child_dest);
                closedir(dp);
                return -1;
            }

            if (lstat(child_src, &child_st) != 0) {
                perror("lstat");
                free(child_src);
                free(child_dest);
                closedir(dp);
                return -1;
            }

            if (S_ISLNK(child_st.st_mode)) {
                /* preserve시 심볼릭 링크 무시 출력문 스킵 */
                // print_skip_symlink_message(child_src);
                continue;
            } else if (S_ISDIR(child_st.st_mode)) {
                /* 현재 entry가 디렉터리인 경우 */

                /* 먼저 디렉터리 속성 보존 */
                if (preserve_file_attributes(child_src, child_dest) != 0) {
                    free(child_src);
                    free(child_dest);
                    closedir(dp);
                    return -1;
                }
                /* 재귀 호출로 넘어갑니다. */
                if (process_preserve(child_src, child_dest) != 0) {
                    free(child_src);
                    free(child_dest);
                    closedir(dp);
                    return -1;
                }
            } else if (S_ISREG(child_st.st_mode)) {
                /* 현재 entry가 일반 파일인 경우 */
                if (preserve_file_attributes(child_src, child_dest) != 0) {
                    free(child_src);
                    free(child_dest);
                    closedir(dp);
                    return -1;
                }
            }
            /* 나머지 파일 종류는 무시합니다. */
            free(child_src);
            free(child_dest);
        }
        closedir(dp);
    } else if (S_ISREG(st.st_mode)) { /* 현재 디렉터리가 아니라 일반 파일이면,
                                         바로 속성을 보존합니다. */
        if (preserve_file_attributes(src, dest) != 0) {
            fprintf(stderr, "Failed to preserve attributes for %s\n", src);
            return -1;
        }
    } else { /* 일반 파일도 아니고 디렉터리도 아닌 경우*/
        fprintf(stderr, "Unsupported file type for %s\n", src);
        return -1;
    }

    return 0;
}

bool file_exists(const char *path) {
    if (access(path, F_OK) == 0) {
        return true;
    } else {
        return false;
    }
}
// src와 name을 합쳐서 동적으로 경로 문자열을 할당하는 함수
char *concat_path(const char *src, const char *name) {
    size_t src_len = strlen(src);
    size_t name_len = strlen(name);
    // '/' + '\0' 포함, 따라서 2바이트 만큼 더 필요함.
    size_t total_len = src_len + 1 + name_len + 1;
    char *buf = malloc(total_len);
    if (!buf) return NULL;
    snprintf(buf, total_len, "%s/%s", src, name);
    return buf;
}
mode_t get_permission_bits(mode_t st_mode) { return st_mode & 0777; }
int safe_mkdir(const char *path, mode_t mode) {
    struct stat st;

    if (lstat(path, &st) != 0) {
        if (mkdir(path, mode) && errno != EEXIST) {
            return -1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        return -1;
    }
    return 0;
}
int mkdirs(const char *dir_path) {
    size_t len = strlen(dir_path) + 1;
    char *buff = malloc(len);
    if (!buff) {
        errno = ENOMEM;
        return -1;
    }
    if (snprintf(buff, len, "%s", dir_path) >= (int)len) {
        free(buff);
        errno = ENAMETOOLONG;
        return -1;
    }
    char *p = buff;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            if (buff[0] != '\0') {
                if (safe_mkdir(buff, DEFAULT_DIRECTORY_MODE) != 0) {
                    free(buff);
                    return -1;
                }
            }
            *p = '/';
        }
        ++p;
    }
    if (buff[0] != '\0') {
        if (safe_mkdir(buff, DEFAULT_DIRECTORY_MODE) != 0) {
            free(buff);
            return -1;
        }
    }
    free(buff);
    return 0;
}