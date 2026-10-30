#define _GNU_SOURCE

#include <fcntl.h>
#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../common/elf_symbol.h"

#define BUFFER_SIZE (64 * 1024)
#define FILE_ROUNDS 8
#define SCHED_CHILDREN 4
#define SCHED_ROUNDS 200

static volatile unsigned long sink;

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

__attribute__((noinline)) void trace_target(int value)
{
    sink += (unsigned long)value * 2654435761UL;
}

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t bytes = write(fd, buffer + written, length - written);

        if (bytes <= 0)
            return -1;
        written += (size_t)bytes;
    }

    return 0;
}

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

    for (i = 0; i < FILE_ROUNDS; i++) {
        if (write_all(fd, buffer, BUFFER_SIZE) != 0)
            break;
    }

    if (lseek(fd, 0, SEEK_SET) == 0) {
        for (i = 0; i < FILE_ROUNDS; i++) {
            if (read(fd, buffer, BUFFER_SIZE) <= 0)
                break;
        }
    }

    close(fd);
    unlink(path);
    free(buffer);
}

static void syscall_workload(void)
{
    char buffer[4096];
    uint64_t end;

    usleep(200000);

    end = monotonic_ns() + 800000000ULL;
    while (monotonic_ns() < end) {
        int i;

        for (i = 0; i < 200; i++)
            sink += (unsigned long)syscall(SYS_getpid);

        {
            int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);

            if (fd >= 0) {
                while (read(fd, buffer, sizeof(buffer)) > 0)
                    ;
                close(fd);
            }
        }
    }
}

static void sched_workload(void)
{
    int started = 0;
    int i;

    for (i = 0; i < SCHED_CHILDREN; i++) {
        pid_t child = fork();

        if (child == 0) {
            int round;

            for (round = 0; round < SCHED_ROUNDS; round++) {
                sched_yield();
                usleep(1000);
            }
            _exit(0);
        } else if (child > 0) {
            started++;
        }
    }

    while (started > 0) {
        if (waitpid(-1, NULL, 0) > 0)
            started--;
        else
            break;
    }
}

static void target_workload(void)
{
    uint64_t end = monotonic_ns() + 300000000ULL;

    while (monotonic_ns() < end)
        trace_target((int)monotonic_ns());
}

static int marker_workload(const char *path)
{
    int fd;
    int i;

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        perror(path);
        return 1;
    }

    for (i = 0; i < 5; i++) {
        if (write_all(fd, "start", 5) != 0)
            break;
        usleep(2000);
        if (write_all(fd, "end", 3) != 0)
            break;
        usleep(2000);
    }

    close(fd);
    return 0;
}

static int print_symbol_offset(void)
{
    char self[PATH_MAX];
    unsigned long long offset = 0;
    ssize_t length;

    length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0) {
        perror("/proc/self/exe");
        return 1;
    }
    self[length] = '\0';

    if (elf_find_symbol_offset(self, "trace_target", &offset) != 0) {
        fprintf(stderr, "trace_target symbol not found in %s\n", self);
        return 1;
    }

    printf("TRACE_TARGET_OFFSET=0x%llx\n", offset);
    return 0;
}

static void usage(const char *program)
{
    printf("Usage: %s <file|syscalls|sched|target|marker <path>|offset>\n",
           program);
}

int main(int argc, char **argv)
{
    const char *mode = "mixed";

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1)
        mode = argv[1];

    if (strcmp(mode, "offset") == 0)
        return print_symbol_offset();

    if (strcmp(mode, "file") == 0) {
        file_workload();
    } else if (strcmp(mode, "syscalls") == 0) {
        syscall_workload();
    } else if (strcmp(mode, "sched") == 0) {
        sched_workload();
    } else if (strcmp(mode, "target") == 0) {
        target_workload();
    } else if (strcmp(mode, "marker") == 0) {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        if (marker_workload(argv[2]) != 0)
            return 1;
    } else if (strcmp(mode, "mixed") == 0) {
        file_workload();
        syscall_workload();
        sched_workload();
        target_workload();
    } else {
        usage(argv[0]);
        return 1;
    }

    printf("TRACE_WORKLOAD_DONE\n");
    return 0;
}
