#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_THREAD 2
#define MAX_RESOURCE 4
#define MAX_NAME_LEN 32  // 스레드 이름 버퍼
#define MAX_STR_LEN 64   // 데드락 출력용 한 줄 버퍼

// 스레드 정보 구조체
typedef struct {
    int tid;                      // 0 → T1, 1 → T2
    int res_count;                // 사용하려는 자원 개수
    int res_order[MAX_RESOURCE];  // 잠금 순서 (예: {0,1} → A,B)
    char name[MAX_NAME_LEN];      // 스레드 이름 캐싱: 스레드 T1(A->B)
} thread_info;

// 데드락 상태 구조체
typedef struct {
    char lines[MAX_THREAD][MAX_STR_LEN];  // "   - 스레드 T1(A->B)" 같은 한 줄
    int count;
    bool alarm_started;
} deadlock_state;

thread_info threads_info[MAX_THREAD];
pthread_mutex_t resource_locks[MAX_RESOURCE];
int wait_for_graph[MAX_THREAD][MAX_THREAD];  // Wait-for graph
bool thread_owns[MAX_THREAD][MAX_RESOURCE];
deadlock_state dstate;

void build_thread_name(thread_info* info);
static void alarm_handler(int signo);
static bool dfs_visit_bool(int node, bool visited[], bool in_stack[]);
bool dfs_cycle(void);
void add_edge(int from, int to);
void start_deadlock_alarm(void);
void print_graph(void);
bool is_locked_by_other(pthread_mutex_t* lock);
int get_resource_owner(int resource);
void lock_resources(thread_info* info);
void release_resources(thread_info* info);
void* worker_thread(void* arg);

int main(void) {
    pthread_t threads[MAX_THREAD];

    memset(wait_for_graph, 0, sizeof(wait_for_graph));
    memset(thread_owns, 0, sizeof(thread_owns));
    memset(&dstate, 0, sizeof(dstate));

    // init resources A, B, C, D ...
    for (int i = 0; i < MAX_RESOURCE; i++) {
        pthread_mutex_init(&resource_locks[i], NULL);
    }

    // thread locking patterns (전역 tinfo에 설정)
    threads_info[0].tid = 0;
    threads_info[0].res_count = 2;
    threads_info[0].res_order[0] = 0;  // A
    threads_info[0].res_order[1] = 1;  // B

    threads_info[1].tid = 1;
    threads_info[1].res_count = 2;
    threads_info[1].res_order[0] = 1;  // B
    threads_info[1].res_order[1] = 0;  // A

    // 스레드 이름 먼저 생성
    for (int i = 0; i < MAX_THREAD; i++) {
        build_thread_name(&threads_info[i]);
    }

    // 스레드 실행
    for (int i = 0; i < MAX_THREAD; i++) {
        pthread_create(&threads[i], NULL, worker_thread, &threads_info[i]);
    }
    for (int i = 0; i < MAX_THREAD; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < MAX_RESOURCE; i++) {
        pthread_mutex_destroy(&resource_locks[i]);
    }

    return 0;
}

// Print current wait-for graph
void print_graph(void) {
    printf("\nWait-for graph:\n");
    bool has_edges = false;
    for (int i = 0; i < MAX_THREAD; i++) {
        for (int j = 0; j < MAX_THREAD; j++) {
            if (wait_for_graph[i][j]) {
                printf("T%d -> T%d\n", i + 1, j + 1);
                has_edges = true;
            }
        }
    }
    if (!has_edges) printf("  (no edges)\n");
}

// 스레드 T1(A->B) 형태로 이름을 생성
// 스레드 이름 생성: "스레드 T1(A->B)" 형태
void build_thread_name(thread_info* info) {
    char lock_order_str[64];
    size_t offset = 0;
    size_t cap = sizeof(lock_order_str);

    // 1) 락 순서 문자열 생성 (A->B->C ...)
    for (int i = 0; i < info->res_count; i++) {
        char res_char = 'A' + info->res_order[i];

        // "A" 또는 "->B" 이런 식으로 이어붙이기
        int written = snprintf(lock_order_str + offset, cap - offset, "%s%c",
                               (i == 0 ? "" : "->"),  // 첫 번째면 -> 없음
                               res_char);

        if (written < 0 || (offset + written) >= cap) {
            break;  // 버퍼 오버 방지
        }

        offset += written;
    }

    // 2) 스레드 전체 이름 생성
    int remaining_chars = sizeof(info->name) - 16;  // prefix/suffix 여유 공간
    if (remaining_chars < 0) remaining_chars = 0;

    snprintf(info->name, sizeof(info->name), "스레드 T%d(%.*s)", info->tid + 1,
             remaining_chars, lock_order_str);
}

static void alarm_handler(int signo) {
    const char deadlock_alert_intro_message[] =
        "\n=== 데드락 감지됨! (5초 경과) ===\n  감지된 스레드들:\n";
    const char deadlock_alert_outro_message[] = "프로그램을 종료합니다.\n";

    bool confirmed_deadlock = dfs_cycle();
    if (confirmed_deadlock) {
        write(STDOUT_FILENO, deadlock_alert_intro_message,
              sizeof(deadlock_alert_intro_message) - 1);

        for (int i = 0; i < dstate.count; i++) {
            const char* p = dstate.lines[i];
            size_t len = strlen(p);
            if (len > 0) {
                write(STDOUT_FILENO, p, len);
            }
        }
        write(STDOUT_FILENO, deadlock_alert_outro_message,
              sizeof(deadlock_alert_outro_message) - 1);
        _exit(0);
    }
    // deadlock이 아니면 프로그램 계속 실행
}

void start_deadlock_alarm(void) {
    // 여러 스레드가 동시에 호출할 수 있으므로 한 번만 실행되도록 함
    if (dstate.alarm_started) return;

    dstate.alarm_started = true;
    signal(SIGALRM, alarm_handler);
    alarm(5);
}

// visited: node was ever visited; in_stack: node is on current recursion stack.
static bool dfs_visit_bool(int node, bool visited[], bool in_stack[]) {
    visited[node] = true;
    in_stack[node] = true;

    for (int next = 0; next < MAX_THREAD; next++) {
        if (!wait_for_graph[node][next]) continue;

        if (!visited[next]) {
            if (dfs_visit_bool(next, visited, in_stack)) return true;
        } else if (in_stack[next]) {
            // back-edge -> cycle; record nodes in current stack and build lines
            dstate.count = 0;
            for (int i = 0; i < MAX_THREAD; i++) {
                if (in_stack[i]) {
                    /* include newline so each entry prints on its own line */
                    snprintf(dstate.lines[dstate.count], MAX_STR_LEN,
                             "   - %s\n", threads_info[i].name);
                    dstate.count++;
                }
            }
            return true;
        }
    }

    in_stack[node] = false;
    return false;
}

bool dfs_cycle(void) {
    bool visited[MAX_THREAD];
    bool in_stack[MAX_THREAD];

    for (int i = 0; i < MAX_THREAD; i++) {
        visited[i] = false;
        in_stack[i] = false;
    }

    for (int i = 0; i < MAX_THREAD; i++) {
        if (!visited[i]) {
            if (dfs_visit_bool(i, visited, in_stack)) return true;
        }
    }
    return false;
}

// 간선 추가 + 사이클 여부 반환 (데드락 처리 분리)
void add_edge(int from, int to) { wait_for_graph[from][to] = 1; }

bool is_locked_by_other(pthread_mutex_t* lock) {
    if (pthread_mutex_trylock(lock) == 0) {
        pthread_mutex_unlock(lock);
        return false;
    }
    return true;
}

int get_resource_owner(int resource) {
    for (int t = 0; t < MAX_THREAD; t++) {
        if (thread_owns[t][resource]) return t;
    }
    return -1;  // 소유자 없음
}

void lock_resources(thread_info* info) {
    int tid = info->tid;

    for (int i = 0; i < info->res_count; i++) {
        int target_resource = info->res_order[i];
        printf("[%s] 자원 %c 잠금 시도\n", info->name, 'A' + target_resource);

        if (is_locked_by_other(&resource_locks[target_resource])) {
            int owner = get_resource_owner(target_resource);
            if (owner < 0) {
                fprintf(stderr,
                        "error: get_owner returned -1 for resource %c\n",
                        'A' + target_resource);
                exit(1);
            }

            printf("[감시기] %s -> %s 를 기다림 (자원 %c)\n", info->name,
                   threads_info[owner].name, 'A' + target_resource);

            // wait-for edge 추가; dfs_cycle은 add_edge 실행 이후 호출
            add_edge(tid, owner);
            // print_graph();
            if (dfs_cycle()) {
                start_deadlock_alarm();
            }
        }

        pthread_mutex_lock(&resource_locks[target_resource]);
        thread_owns[tid][target_resource] = true;
        printf("[%s] 자원 %c 잠금 성공\n", info->name, 'A' + target_resource);

        sleep(1);
    }
}

void release_resources(thread_info* info) {
    int tid = info->tid;
    for (int i = info->res_count - 1; i >= 0; i--) {
        int r = info->res_order[i];
        thread_owns[tid][r] = false;
        pthread_mutex_unlock(&resource_locks[r]);
    }
}

// 스레드 실행 함수
void* worker_thread(void* arg) {
    thread_info* info = (thread_info*)arg;

    lock_resources(info);
    release_resources(info);

    return NULL;
}
