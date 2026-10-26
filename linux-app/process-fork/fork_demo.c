#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILDREN 3

int main(void)
{
    pid_t pids[CHILDREN];
    int failures = 0;
    int i;

    /* 子进程用 _exit() 退出，关闭 stdout 缓冲避免输出丢失 */
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < CHILDREN; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pids[i] == 0) {
            printf("child %d: pid=%ld ppid=%ld\n",
                   i, (long)getpid(), (long)getppid());
            _exit(10 + i);
        }
    }

    /* 父进程回收所有子进程，并检查各自的退出码 */
    for (i = 0; i < CHILDREN; i++) {
        int status;
        pid_t pid = waitpid(pids[i], &status, 0);

        if (pid < 0) {
            perror("waitpid");
            failures++;
            continue;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 10 + i) {
            printf("parent: child %d exited with code %d\n",
                   i, WEXITSTATUS(status));
        } else {
            fprintf(stderr, "parent: child %d exited abnormally (status=0x%x)\n",
                    i, (unsigned int)status);
            failures++;
        }
    }

    if (failures != 0) {
        fprintf(stderr, "fork: FAIL (%d errors)\n", failures);
        return EXIT_FAILURE;
    }

    printf("fork: PASS\n");
    return EXIT_SUCCESS;
}
