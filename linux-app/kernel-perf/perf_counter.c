#define _GNU_SOURCE

#include <errno.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define MEMORY_WORKLOAD_SIZE (8 * 1024 * 1024)

struct counter {
    const char *name;
    uint32_t type;
    uint64_t config;
    int fd;
    int available;
    uint64_t value;
};

static struct counter counters[] = {
    { "task-clock", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, -1, 0, 0 },
    { "context-switches", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES, -1, 0, 0 },
    { "page-faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, -1, 0, 0 },
    { "cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1, 0, 0 },
    { "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, -1, 0, 0 },
    { "branch-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES, -1, 0, 0 },
    { "cache-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES, -1, 0, 0 },
};

static volatile uint64_t sink;

static int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags)
{
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int open_counter(struct counter *counter)
{
    struct perf_event_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.type = counter->type;
    attr.size = sizeof(attr);
    attr.config = counter->config;
    attr.disabled = 1;
    attr.exclude_hv = 1;

    counter->fd = perf_event_open(&attr, 0, -1, -1, 0);
    if (counter->fd < 0) {
        counter->available = 0;
        return -1;
    }

    counter->available = 1;
    return 0;
}

static void ioctl_counters(unsigned long command)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (counters[i].available)
            ioctl(counters[i].fd, command, 0);
    }
}

static void read_counters(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        ssize_t bytes;

        if (!counters[i].available)
            continue;

        bytes = read(counters[i].fd, &counters[i].value,
                     sizeof(counters[i].value));
        if (bytes != (ssize_t)sizeof(counters[i].value))
            counters[i].value = 0;
    }
}

static void close_counters(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (counters[i].available)
            close(counters[i].fd);
    }
}

static void workload_cpu(uint64_t duration_ms)
{
    uint64_t end = monotonic_ns() + duration_ms * 1000000ULL;
    uint64_t value = 0x9e3779b97f4a7c15ULL;

    while (monotonic_ns() < end) {
        int i;

        for (i = 0; i < 10000; i++) {
            value ^= value << 13;
            value ^= value >> 7;
            value ^= value << 17;
            value += (uint64_t)i;
        }
    }

    sink ^= value;
}

static void workload_syscall(uint64_t duration_ms)
{
    uint64_t end = monotonic_ns() + duration_ms * 1000000ULL;
    uint64_t calls = 0;

    while (monotonic_ns() < end) {
        int i;

        for (i = 0; i < 1000; i++) {
            sink += (uint64_t)getpid();
            calls++;
        }
    }

    printf("  getpid calls: %llu\n", (unsigned long long)calls);
}

static void workload_memory(uint64_t duration_ms)
{
    size_t elements = MEMORY_WORKLOAD_SIZE / sizeof(uint64_t);
    volatile uint64_t *buffer = malloc(MEMORY_WORKLOAD_SIZE);
    uint64_t end;
    uint64_t sum = 0;
    size_t i;

    if (buffer == NULL) {
        perror("malloc");
        return;
    }

    for (i = 0; i < elements; i++)
        buffer[i] = (uint64_t)i * 0x9e3779b97f4a7c15ULL;

    end = monotonic_ns() + duration_ms * 1000000ULL;
    while (monotonic_ns() < end) {
        for (i = 0; i < elements; i++)
            sum += buffer[i];
    }

    free((void *)buffer);
    sink += sum;
}

static void run_workload(const char *mode, uint64_t duration_ms)
{
    if (strcmp(mode, "cpu") == 0)
        workload_cpu(duration_ms);
    else if (strcmp(mode, "syscall") == 0)
        workload_syscall(duration_ms);
    else if (strcmp(mode, "memory") == 0)
        workload_memory(duration_ms);
    else {
        workload_cpu(duration_ms);
        workload_syscall(duration_ms);
        workload_memory(duration_ms);
    }
}

static void usage(const char *program)
{
    printf("Usage: %s [cpu|syscall|memory|all] [duration_ms]\n", program);
    printf("  default: all 500\n");
}

int main(int argc, char **argv)
{
    const char *mode = "all";
    uint64_t duration_ms = 500;
    uint64_t wall_start;
    uint64_t wall_end;
    size_t i;
    int available = 0;

    if (argc > 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        mode = argv[1];
        if (strcmp(mode, "cpu") != 0 && strcmp(mode, "syscall") != 0 &&
            strcmp(mode, "memory") != 0 && strcmp(mode, "all") != 0) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (argc > 2) {
        char *end = NULL;

        errno = 0;
        duration_ms = strtoull(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' ||
            duration_ms < 10 || duration_ms > 60000) {
            fprintf(stderr, "Invalid duration: %s (10..60000 ms)\n", argv[2]);
            return EXIT_FAILURE;
        }
    }

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (open_counter(&counters[i]) == 0)
            available++;
    }

    if (available == 0) {
        fprintf(stderr, "perf_event_open failed for every event: %s\n",
                strerror(errno));
        fprintf(stderr, "Run as root or lower kernel.perf_event_paranoid.\n");
        return EXIT_FAILURE;
    }

    ioctl_counters(PERF_EVENT_IOC_RESET);
    ioctl_counters(PERF_EVENT_IOC_ENABLE);

    wall_start = monotonic_ns();
    run_workload(mode, duration_ms);
    wall_end = monotonic_ns();

    ioctl_counters(PERF_EVENT_IOC_DISABLE);
    read_counters();

    printf("\nmode: %s, requested: %llu ms, wall: %llu ms\n",
           mode, (unsigned long long)duration_ms,
           (unsigned long long)((wall_end - wall_start) / 1000000ULL));
    printf("%-20s %20s\n", "event", "count");
    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (counters[i].available)
            printf("%-20s %20llu\n", counters[i].name,
                   (unsigned long long)counters[i].value);
    }

    if (counters[3].available && counters[3].value > 0 &&
        counters[4].available) {
        double ipc = (double)counters[4].value /
                     (double)counters[3].value;

        printf("IPC (instructions/cycles): %.2f\n", ipc);
    }

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (!counters[i].available)
            printf("unsupported: %s\n", counters[i].name);
    }

    close_counters();
    return EXIT_SUCCESS;
}
