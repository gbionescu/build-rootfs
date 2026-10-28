#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define BUFFER_SIZE 65536
#define ROUNDS 8

static void file_workload(void)
{
    char path[] = "/tmp/trace-workload-XXXXXX";
    char *buffer = malloc(BUFFER_SIZE);
    int fd;
    int i;

    if (buffer == NULL)
        return;

    for (i = 0; i < BUFFER_SIZE; i++)
        buffer[i] = (char)('A' + i % 26);

    fd = mkstemp(path);
    if (fd < 0) {
        free(buffer);
        return;
    }

    for (i = 0; i < ROUNDS; i++) {
        if (write(fd, buffer, BUFFER_SIZE) != BUFFER_SIZE)
            break;
    }

    if (lseek(fd, 0, SEEK_SET) == 0) {
        for (i = 0; i < ROUNDS; i++) {
            if (read(fd, buffer, BUFFER_SIZE) <= 0)
                break;
        }
    }

    close(fd);
    unlink(path);
    free(buffer);
}

static void proc_workload(void)
{
    char buffer[4096];
    int i;

    for (i = 0; i < ROUNDS; i++) {
        int fd = open("/proc/self/status", O_RDONLY);

        if (fd < 0)
            return;

        while (read(fd, buffer, sizeof(buffer)) > 0)
            ;
        close(fd);
    }
}

int main(void)
{
    pid_t pid;

    setvbuf(stdout, NULL, _IONBF, 0);

    file_workload();
    proc_workload();

    pid = fork();
    if (pid == 0) {
        volatile unsigned long value = 1;
        int i;

        for (i = 0; i < 500000; i++)
            value = value * 1664525UL + 1013904223UL;
        usleep(100000);
        _exit(0);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    }

    usleep(50000);
    printf("TRACE_WORKLOAD_DONE\n");
    return EXIT_SUCCESS;
}
