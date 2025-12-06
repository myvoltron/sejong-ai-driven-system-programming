#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>

#define GENERATE_API_URL "http://localhost:11434/api/generate"
#define CHAT_API_URL "http://localhost:11434/api/chat"
#define MODEL_NAME_GEMMA3_1B "gemma3:1b"

#define SHM_NAME "/ai_shm"
#define SEM_TO_AI "/sem_to_ai"
#define SEM_TO_PARENT "/sem_to_parent"
#define PROMPT_FILE "prompt_engineering.txt"

typedef struct {
    char prompt[4096];
    char response[8192];
} ShmBuf;

// 응답 끝을 알리는 마커
char* END_MARKER = "\n<<<END>>>\n";

typedef struct {
    char* data;
    size_t size;
} MemoryStruct;

// --- 함수 원형 ---
int ai_chat_with_context(char* log_mem, size_t* cm_size,
                         const char* user_input, char* assistant_output,
                         size_t out_size, const char* model);
static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
static void append_log(char* context_mem, size_t* cm_size, const char* role, const char* text);
static cJSON* build_messages_json(char* context_mem, size_t cm_size, const char *sys_prompt_path);
static int call_chat_api(cJSON* messages, const char* model, char* out, size_t out_sz);

// [추가] 파일 내용을 메모리 버퍼로 읽는 헬퍼 함수
static char* read_file_to_buffer(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);

    // 모델 컨텍스트 윈도우를 넘지 않도록 최대 크기 제한 (8KB)
    if (fsize > 8192) fsize = 8192; 
    
    char *buffer = malloc(fsize + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }

    // 파일 크기만큼 읽고 null 종료
    size_t bytes_read = fread(buffer, 1, fsize, fp);
    buffer[bytes_read] = '\0';
    fclose(fp);
    return buffer;
}


// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* LOG_PATH = "prompt.log";

    // 1. Context Memory (prompt.log) 초기화
    // O_TRUNC로 시작할 때마다 로그 초기화 (깨끗한 상태 시작)
    int fd = open(LOG_PATH, O_RDWR | O_CREAT | O_TRUNC, 0644); 
    if (fd < 0) { perror("open"); return 1; }

    size_t capacity = 1024 * 1024;
    ftruncate(fd, capacity);
    char* context_mem = (char*)mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (context_mem == MAP_FAILED) { perror("mmap"); return 1; }

    size_t cm_size = 0; // 초기화된 상태로 시작

    // POSIX IPC Setup
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) { perror("shm_open"); return 1; }

    ShmBuf* shm = mmap(NULL, sizeof(ShmBuf), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) { perror("mmap shm"); return 1; }

    sem_t* sem_to_ai = sem_open(SEM_TO_AI, 0);
    sem_t* sem_to_parent = sem_open(SEM_TO_PARENT, 0);

    char prompt[4096];
    char raw_response[8192];

    while (1) {
        sem_wait(sem_to_ai);

        strncpy(prompt, shm->prompt, sizeof(prompt) - 1);
        prompt[sizeof(prompt) - 1] = '\0';

        if (strncmp(prompt, "exit", 4) == 0) break;

        raw_response[0] = '\0';
        
        // 매번 PROMPT_FILE을 읽어 동적 시스템 프롬프트 주입
        int ret = ai_chat_with_context(context_mem, &cm_size, prompt, 
                                       raw_response, sizeof(raw_response), 
                                       MODEL_NAME_GEMMA3_1B);

        if (ret != 0) {
            snprintf(shm->response, sizeof(shm->response), "[AI ERROR]");
        } else {
            size_t marker_len = strlen(END_MARKER);
            size_t raw_len = strlen(raw_response);
            if (raw_len + marker_len < sizeof(shm->response)) {
                snprintf(shm->response, sizeof(shm->response), "%s%s", raw_response, END_MARKER);
            } else {
                strncpy(shm->response, raw_response, sizeof(shm->response)-1);
            }
        }
        sem_post(sem_to_parent);
    }

    munmap(shm, sizeof(ShmBuf));
    close(shm_fd);
    sem_close(sem_to_ai);
    sem_close(sem_to_parent);
    
    // 로그 파일 정리
    munmap(context_mem, capacity);
    close(fd);
    return 0;
}

// ---------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------

// 함수 시그니처 변경: sys_prompt_path 인자 추가됨 (매크로 사용)
int ai_chat_with_context(char* log_mem, size_t* cm_size, const char* user_input, char* assistant_output, size_t out_size, const char* model) {
    
    // 1. 사용자 질문을 로그에 기록 (과거 대화 로그 갱신)
    append_log(log_mem, cm_size, "USER", user_input);
    
    // 2. 로그와 함께 최신 시스템 프롬프트를 합쳐서 메시지 배열 생성
    // prompt.log (log_mem)의 내용을 기반으로 메시지 배열을 만들고, 
    // 그 배열의 맨 앞에 최신 prompt_engineering.txt 내용을 추가합니다.
    cJSON* messages_json = build_messages_json(log_mem, *cm_size, PROMPT_FILE);
    if (!messages_json) return -1;
    
    // 3. API 호출
    int ret = call_chat_api(messages_json, model, assistant_output, out_size);
    // call_chat_api가 root에 messages_json을 attach하고 root를 delete하므로
    // messages_json의 소유권은 call_chat_api로 이전됨. 여기서 삭제하지 않음.
    if (ret != 0) {
        return -1;
    }
    
    // 4. AI 응답을 로그에 기록 (과거 대화 로그 갱신)
    append_log(log_mem, cm_size, "ASSISTANT", assistant_output);
    return 0;
}

static cJSON* build_messages_json(char* context_mem, size_t cm_size, const char *sys_prompt_path) {
    cJSON* messages = cJSON_CreateArray();
    
    // 1. [핵심] 최신 시스템 프롬프트 파일을 읽어서 메시지 배열의 맨 앞에 삽입
    char *sys_prompt_content = read_file_to_buffer(sys_prompt_path);
    if (sys_prompt_content) {
        cJSON* sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", sys_prompt_content);
        cJSON_AddItemToArray(messages, sys_msg); // 맨 앞에 삽입
        free(sys_prompt_content);
    } else {
        // 파일을 읽지 못해도 오류는 아니지만, 빈 시스템 메시지를 넣어주는 것이 안전
        cJSON* sys_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(sys_msg, "role", "system");
        cJSON_AddStringToObject(sys_msg, "content", "You are a helpful Ubuntu assistant.");
        cJSON_AddItemToArray(messages, sys_msg);
    }
    
    if (cm_size == 0) return messages; // 과거 대화 로그가 없으면 여기서 종료

    // 2. 과거 대화 로그 (context_mem)를 읽어서 메시지 배열에 순차적으로 추가
    char* ptr = context_mem;
    char* end = context_mem + cm_size;

    while (ptr < end) {
        char* line_end = memchr(ptr, '\n', end - ptr);
        if (!line_end) line_end = end;
        size_t line_len = line_end - ptr;
        
        if (line_len == 0) {
            ptr = line_end + 1;
            continue;
        }

        char *separator = memchr(ptr, ':', line_len);
        if (separator) {
            char role[32] = {0};
            char content[4096] = {0}; 
            
            size_t role_len = separator - ptr;
            if (role_len > 31) role_len = 31;
            memcpy(role, ptr, role_len);
            role[role_len] = '\0';
            
            char *content_start = separator + 2;
            
            if (content_start < line_end) {
                size_t content_len = line_end - content_start;
                if (content_len >= sizeof(content)) content_len = sizeof(content) - 1;
                
                memcpy(content, content_start, content_len);
                content[content_len] = '\0';
            }

            cJSON* msg = cJSON_CreateObject();
            // 과거 로그에서 "system" 메시지는 건너뜁니다. (최신 시스템 메시지가 이미 맨 앞에 있으므로)
            if (strcasecmp(role, "system") == 0) {
                 // skip
            }
            else if (strcasecmp(role, "USER") == 0) {
                cJSON_AddStringToObject(msg, "role", "user");
                cJSON_AddStringToObject(msg, "content", content);
                cJSON_AddItemToArray(messages, msg);
            }
            else if (strcasecmp(role, "ASSISTANT") == 0) {
                cJSON_AddStringToObject(msg, "role", "assistant");
                cJSON_AddStringToObject(msg, "content", content);
                cJSON_AddItemToArray(messages, msg);
            }
            else {
                cJSON_Delete(msg);
            }
        }
        ptr = line_end + 1;
    }
    return messages;
}

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    MemoryStruct* mem = (MemoryStruct*)userp;
    char* ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    return realsize;
}

static void append_log(char* context_mem, size_t* cm_size, const char* role, const char* text) {
    size_t max_write = 1024 * 1024 - *cm_size;
    if (max_write < 100) return;
    int len = snprintf(context_mem + *cm_size, max_write, "%s: %s\n", role, text);
    if (len > 0 && (size_t)len < max_write) *cm_size += len;
}

static int call_chat_api(cJSON* messages, const char* model, char* out, size_t out_sz) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddItemToObject(root, "messages", messages);
    cJSON_AddBoolToObject(root, "stream", 0);
    cJSON_AddNumberToObject(root, "temperature", 0.3); 

    char* json_payload = cJSON_PrintUnformatted(root);
    MemoryStruct resp = {0};
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, CHAT_API_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(json_payload);
    cJSON_Delete(root);

    if (res != CURLE_OK || !resp.data) { free(resp.data); return -1; }

    cJSON* parsed = cJSON_Parse(resp.data);
    if (!parsed) { free(resp.data); return -1; }

    cJSON* msg = cJSON_GetObjectItem(parsed, "message");
    cJSON* content = msg ? cJSON_GetObjectItem(msg, "content") : NULL;
    if (content && content->valuestring) {
        strncpy(out, content->valuestring, out_sz - 1);
        out[out_sz - 1] = '\0';
    } else {
        cJSON_Delete(parsed);
        free(resp.data);
        return -1;
    }
    cJSON_Delete(parsed);
    free(resp.data);
    return 0;
}