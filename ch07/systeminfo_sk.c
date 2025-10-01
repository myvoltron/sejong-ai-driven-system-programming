#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/utsname.h>
#include <string.h>

int main(void) {
	struct timespec start, end;
	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
		perror("clock_gettime(start)");
		return 1;
	}
	// 사용자 정보
	char *login = getlogin();
	if (!login) {
		fprintf(stderr, "getlogin() failed\n");		
	}
	struct passwd *pw = getpwnam(login);
	if (pw) {
		printf("[User Info]\n");
		printf("user: %s, uid=%d, gid=%d, home=%s, shell=%s", pw->pw_name, pw->pw_uid, pw->pw_gid, pw->pw_dir, pw->pw_shell);
		// your code here
	} else {
		fprintf(stderr, "getpwnam() failed for %s\n", login);
	}

	// 그룹 정보, linux는 getgroups()에 주 그룹도 포함됨. 
	int ngroups = getgroups(0, NULL); 
	if (ngroups < 0) {
		perror("getgroups(count)");
		ngroups = 0;
	}
	gid_t groups[ngroups > 0 ? ngroups : 1];
	if (getgroups(ngroups, groups) == -1) {
		perror("getgroups");
	}

	printf("\n[Group Info]\n");
	for (int i = 0; i < ngroups; i++) {
		printf("Group ID %d: %d\n", i, groups[i]);
	}
	printf("\n");

	// 로그인 정보
	printf("\n[Login Info]\nlogin name: %s\n", login);

	// 호스트 정보
	char hostname[256] = {0};
	if ((gethostname(hostname, strlen(hostname))) != 0) {
		perror("gethostname");
		strcpy(hostname, "unknown");
	}
	struct utsname uts;
	if ((uname(&uts)) != 0) {
		perror("uname");
		memset(&uts, 0, sizeof(uts));
	}
	printf("\n[Host Info]\n");
	printf("hostname: %s\n", hostname);
	printf("uname: %s %s %s %s\n", );

	// 시간 정보
	time_t now = time(NULL);
	if (now == (time_t)-1) { 
		perror("time");
		now = 0;
	}
	char buf[64] = {0};
	struct tm *lt = localtime(&now);
	if (!lt) {
		perror("localtime");
		strncpy(buf, "unknown", sizeof(buf)-1);
	} else {
		// your code here		
	}
	printf("\n[Time Info]\nepoch: %ld\nlocaltime: %s\n", now, buf);

	// 실행 시간
	if (// your code here ) {
		perror("clock_gettime(end)");
		return 1;
	}
	double elapsed = // your code here ;
	printf("elapsed: %.9f sec\n", elapsed);

	return 0;
}
