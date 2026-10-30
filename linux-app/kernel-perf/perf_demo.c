#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../common/elf_symbol.h"

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define MEMORY_WORKLOAD_SIZE (8 * 1024 * 1024)
#define FILE_WORKLOAD_SIZE (64 * 1024)
#define SAMPLE_PAGES 8
#define MAX_KEPT_SAMPLES 5
#define MAX_CALLCHAIN 16

static volatile uint64_t sink;

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
    { "context-switches", PERF_TYPE_SOFTWARE,
      PERF_COUNT_SW_CONTEXT_SWITCHES, -1, 0, 0 },
    { "page-faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, -1, 0, 0 },
    { "cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1, 0, 0 },
    { "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, -1, 0,
      0 },
    { "branch-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES, -1, 0,
      0 },
    { "cache-misses", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES, -1, 0,
      0 },
};

struct sample_record {
    uint64_t ip;
    uint32_t pid;
    uint32_t tid;
    uint64_t time;
    uint32_t cpu;
    uint64_t nr;
    uint64_t callchain[MAX_CALLCHAIN];
};

static int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu,
                           int group_fd, unsigned long flags)
{
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd,
                        flags);
}

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int read_counter_value(int fd, unsigned long long *value)
{
    ssize_t bytes = read(fd, value, sizeof(*value));

    return bytes == (ssize_t)sizeof(*value) ? 0 : -1;
}

static const char *find_tracefs(void)
{
    static char tracefs[PATH_MAX];
    static const char *candidates[] = {
        "/sys/kernel/tracing",
        "/sys/kernel/debug/tracing",
    };
    char path[PATH_MAX];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(candidates); i++) {
        snprintf(path, sizeof(path), "%s/available_tracers", candidates[i]);
        if (access(path, F_OK) == 0) {
            snprintf(tracefs, sizeof(tracefs), "%s", candidates[i]);
            return tracefs;
        }
    }

    return NULL;
}

static int read_uint_file(const char *path, unsigned long long *value)
{
    FILE *file;
    int rc;

    file = fopen(path, "r");
    if (file == NULL)
        return -1;

    rc = fscanf(file, "%llu", value) == 1 ? 0 : -1;
    fclose(file);
    return rc;
}

static int write_string_file(const char *path, const char *value)
{
    size_t length = strlen(value);
    ssize_t written;
    int fd;

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;

    written = write(fd, value, length);
    close(fd);
    return written == (ssize_t)length ? 0 : -1;
}

static int tracefs_event_id(const char *group, const char *event,
                            unsigned long long *id)
{
    char path[PATH_MAX];
    const char *tracefs = find_tracefs();

    if (tracefs == NULL)
        return -1;

    snprintf(path, sizeof(path), "%s/events/%s/%s/id", tracefs, group,
             event);
    return read_uint_file(path, id);
}

static int parse_duration(int argc, char **argv, uint64_t *duration_ms)
{
    char *end = NULL;

    *duration_ms = 500;
    if (argc == 0)
        return 0;

    errno = 0;
    *duration_ms = strtoull(argv[0], &end, 10);
    if (errno != 0 || end == argv[0] || *end != '\0' || *duration_ms < 10 ||
        *duration_ms > 60000) {
        fprintf(stderr, "Invalid duration: %s (10..60000 ms)\n", argv[0]);
        return -1;
    }

    return 0;
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

static void workload_file(uint64_t duration_ms)
{
    char path[] = "/tmp/perf-demo-XXXXXX";
    char *buffer = malloc(FILE_WORKLOAD_SIZE);
    uint64_t end;
    int fd;
    int i;

    if (buffer == NULL)
        return;

    fd = mkstemp(path);
    if (fd < 0) {
        free(buffer);
        return;
    }

    for (i = 0; i < FILE_WORKLOAD_SIZE; i++)
        buffer[i] = (char)('A' + i % 26);
    if (write(fd, buffer, FILE_WORKLOAD_SIZE) != FILE_WORKLOAD_SIZE) {
        close(fd);
        unlink(path);
        free(buffer);
        return;
    }

    end = monotonic_ns() + duration_ms * 1000000ULL;
    while (monotonic_ns() < end) {
        if (lseek(fd, 0, SEEK_SET) < 0)
            break;
        if (read(fd, buffer, FILE_WORKLOAD_SIZE) <= 0)
            break;
    }

    close(fd);
    unlink(path);
    free(buffer);
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

static struct counter *find_counter(const char *name)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (strcmp(counters[i].name, name) == 0)
            return &counters[i];
    }

    return NULL;
}

static int mode_stat(int argc, char **argv)
{
    uint64_t duration_ms;
    uint64_t per_workload;
    uint64_t wall_start;
    uint64_t wall_end;
    size_t i;
    int available = 0;

    if (parse_duration(argc, argv, &duration_ms) != 0)
        return 1;

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (open_counter(&counters[i]) == 0)
            available++;
    }

    if (available == 0) {
        fprintf(stderr, "perf_event_open failed for every event: %s\n",
                strerror(errno));
        fprintf(stderr, "Run as root or lower kernel.perf_event_paranoid.\n");
        return 2;
    }

    per_workload = duration_ms / 3;
    if (per_workload < 10)
        per_workload = 10;

    ioctl_counters(PERF_EVENT_IOC_RESET);
    ioctl_counters(PERF_EVENT_IOC_ENABLE);

    wall_start = monotonic_ns();
    workload_cpu(per_workload);
    workload_syscall(per_workload);
    workload_memory(per_workload);
    wall_end = monotonic_ns();

    ioctl_counters(PERF_EVENT_IOC_DISABLE);
    read_counters();

    printf("\nmode: stat, requested: %llu ms, wall: %llu ms\n",
           (unsigned long long)duration_ms,
           (unsigned long long)((wall_end - wall_start) / 1000000ULL));
    printf("%-20s %20s\n", "event", "count");
    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (counters[i].available)
            printf("%-20s %20llu\n", counters[i].name,
                   (unsigned long long)counters[i].value);
    }

    {
        struct counter *cycles = find_counter("cycles");
        struct counter *instructions = find_counter("instructions");

        if (cycles != NULL && instructions != NULL && cycles->available &&
            cycles->value > 0 && instructions->available)
            printf("IPC (instructions/cycles): %.2f\n",
                   (double)instructions->value / (double)cycles->value);
    }

    for (i = 0; i < ARRAY_SIZE(counters); i++) {
        if (!counters[i].available)
            printf("unsupported: %s\n", counters[i].name);
    }

    close_counters();
    return 0;
}

struct group_member {
    const char *name;
    uint32_t type;
    uint64_t config;
    int fd;
    int available;
};

static int mode_group(int argc, char **argv)
{
    struct group_member members[] = {
        { "context-switches", PERF_TYPE_SOFTWARE,
          PERF_COUNT_SW_CONTEXT_SWITCHES, -1, 0 },
        { "page-faults", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, -1, 0 },
        { "instructions", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, -1,
          0 },
        { "cycles", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1, 0 },
    };
    struct {
        uint64_t nr;
        uint64_t time_enabled;
        uint64_t time_running;
        uint64_t values[ARRAY_SIZE(members) + 1];
    } group;
    struct perf_event_attr attr;
    uint64_t value_index = 1;
    ssize_t bytes;
    size_t i;
    int leader;
    int opened = 0;

    (void)argc;
    (void)argv;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_TASK_CLOCK;
    attr.disabled = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_GROUP |
                       PERF_FORMAT_TOTAL_TIME_ENABLED |
                       PERF_FORMAT_TOTAL_TIME_RUNNING;

    leader = perf_event_open(&attr, 0, -1, -1, 0);
    if (leader < 0) {
        fprintf(stderr, "group leader perf_event_open failed: %s\n",
                strerror(errno));
        return 2;
    }

    for (i = 0; i < ARRAY_SIZE(members); i++) {
        memset(&attr, 0, sizeof(attr));
        attr.type = members[i].type;
        attr.size = sizeof(attr);
        attr.config = members[i].config;
        attr.disabled = 0;
        attr.exclude_hv = 1;

        members[i].fd = perf_event_open(&attr, 0, -1, leader, 0);
        if (members[i].fd >= 0) {
            members[i].available = 1;
            opened++;
        }
    }

    ioctl(leader, PERF_EVENT_IOC_RESET, 0);
    ioctl(leader, PERF_EVENT_IOC_ENABLE, 0);
    workload_cpu(300);
    workload_memory(200);
    ioctl(leader, PERF_EVENT_IOC_DISABLE, 0);

    memset(&group, 0, sizeof(group));
    bytes = read(leader, &group, sizeof(group));
    if (bytes < (ssize_t)(3 * sizeof(uint64_t))) {
        fprintf(stderr, "group read failed: %s\n", strerror(errno));
        close(leader);
        for (i = 0; i < ARRAY_SIZE(members); i++) {
            if (members[i].available)
                close(members[i].fd);
        }
        return 1;
    }

    printf("\nmode: group, members: %d\n", opened + 1);
    printf("%-20s %20s\n", "event", "count");
    printf("%-20s %20llu\n", "task-clock",
           (unsigned long long)group.values[0]);
    for (i = 0; i < ARRAY_SIZE(members); i++) {
        if (members[i].available && value_index < group.nr)
            printf("%-20s %20llu\n", members[i].name,
                   (unsigned long long)group.values[value_index++]);
    }

    if (group.time_running != 0 && group.time_running < group.time_enabled) {
        double scale = (double)group.time_enabled /
                       (double)group.time_running;

        printf("multiplexed: enabled=%llu running=%llu scale=%.2f\n",
               (unsigned long long)group.time_enabled,
               (unsigned long long)group.time_running, scale);
    } else {
        printf("not multiplexed: enabled=%llu running=%llu\n",
               (unsigned long long)group.time_enabled,
               (unsigned long long)group.time_running);
    }

    close(leader);
    for (i = 0; i < ARRAY_SIZE(members); i++) {
        if (members[i].available)
            close(members[i].fd);
    }

    return opened > 0 ? 0 : 2;
}

static void parse_sample(struct perf_event_header *header,
                         struct sample_record *record)
{
    unsigned char *cursor = (unsigned char *)header + sizeof(*header);
    uint32_t pid;
    uint32_t tid;
    uint32_t cpu;
    uint32_t reserved;
    uint64_t nr;
    uint64_t i;

    memset(record, 0, sizeof(*record));

    memcpy(&record->ip, cursor, sizeof(record->ip));
    cursor += sizeof(record->ip);
    memcpy(&pid, cursor, sizeof(pid));
    cursor += sizeof(pid);
    memcpy(&tid, cursor, sizeof(tid));
    cursor += sizeof(tid);
    memcpy(&record->time, cursor, sizeof(record->time));
    cursor += sizeof(record->time);
    memcpy(&cpu, cursor, sizeof(cpu));
    cursor += sizeof(cpu);
    memcpy(&reserved, cursor, sizeof(reserved));
    cursor += sizeof(reserved);
    (void)reserved;
    memcpy(&nr, cursor, sizeof(nr));
    cursor += sizeof(nr);

    record->pid = pid;
    record->tid = tid;
    record->cpu = cpu;
    record->nr = nr > MAX_CALLCHAIN ? MAX_CALLCHAIN : nr;

    for (i = 0; i < record->nr; i++) {
        memcpy(&record->callchain[i], cursor, sizeof(record->callchain[i]));
        cursor += sizeof(record->callchain[i]);
    }
}

static int mode_sample(int argc, char **argv)
{
    struct sample_record kept[MAX_KEPT_SAMPLES];
    struct perf_event_attr attr;
    struct perf_event_mmap_page *meta;
    char *data;
    size_t data_size;
    size_t mmap_size;
    long page_size;
    uint64_t duration_ms;
    uint64_t samples = 0;
    uint64_t lost = 0;
    int kept_count = 0;
    int fd;

    if (parse_duration(argc, argv, &duration_ms) != 0)
        return 1;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.disabled = 1;
    attr.exclude_hv = 1;
    attr.sample_period = 1000000;
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME |
                       PERF_SAMPLE_CPU | PERF_SAMPLE_CALLCHAIN;
    attr.sample_max_stack = MAX_CALLCHAIN;
    attr.wakeup_events = 1;

    fd = perf_event_open(&attr, 0, -1, -1, 0);
    if (fd < 0) {
        fprintf(stderr, "sampling perf_event_open failed: %s\n",
                strerror(errno));
        return 2;
    }

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        close(fd);
        return 1;
    }
    data_size = (size_t)SAMPLE_PAGES * (size_t)page_size;
    mmap_size = data_size + (size_t)page_size;

    meta = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (meta == MAP_FAILED) {
        fprintf(stderr, "mmap ring buffer failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    data = (char *)meta + page_size;
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    workload_cpu(duration_ms);
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    {
        uint64_t head = meta->data_head;
        uint64_t tail;

        __sync_synchronize();
        tail = meta->data_tail;

        while (tail < head) {
            struct perf_event_header *header =
                (struct perf_event_header *)(data + (tail % data_size));

            if (header->size < sizeof(*header) ||
                header->size > data_size)
                break;

            if (header->type == PERF_RECORD_SAMPLE) {
                struct sample_record record;

                parse_sample(header, &record);
                samples++;
                if (kept_count < MAX_KEPT_SAMPLES)
                    kept[kept_count++] = record;
            } else if (header->type == PERF_RECORD_LOST) {
                struct {
                    struct perf_event_header header;
                    uint64_t id;
                    uint64_t lost;
                } *lost_record = (void *)header;

                lost += lost_record->lost;
            }

            tail += header->size;
        }

        meta->data_tail = head;
    }

    printf("\nmode: sample, requested: %llu ms\n",
           (unsigned long long)duration_ms);
    printf("samples: %llu, lost: %llu\n", (unsigned long long)samples,
           (unsigned long long)lost);

    for (int i = 0; i < kept_count; i++) {
        printf("sample[%d]: ip=0x%llx pid=%u tid=%u cpu=%u time=%llu frames=%llu\n",
               i, (unsigned long long)kept[i].ip, kept[i].pid, kept[i].tid,
               kept[i].cpu, (unsigned long long)kept[i].time,
               (unsigned long long)kept[i].nr);
        for (uint64_t frame = 0; frame < kept[i].nr; frame++)
            printf("  #%llu 0x%llx\n", (unsigned long long)frame,
                   (unsigned long long)kept[i].callchain[frame]);
    }

    munmap(meta, mmap_size);
    close(fd);

    if (samples == 0)
        return 1;
    return 0;
}

static int open_tracepoint_counter(const char *group, const char *event,
                                   unsigned long long *id)
{
    struct perf_event_attr attr;
    int fd;

    if (tracefs_event_id(group, event, id) != 0)
        return -1;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_TRACEPOINT;
    attr.size = sizeof(attr);
    attr.config = *id;
    attr.disabled = 1;
    attr.exclude_hv = 1;

    fd = perf_event_open(&attr, 0, -1, -1, 0);
    return fd;
}

static int mode_tracepoint(int argc, char **argv)
{
    struct {
        const char *group;
        const char *event;
    } candidates[] = {
        { "raw_syscalls", "sys_enter" },
        { "sched", "sched_switch" },
    };
    unsigned long long id = 0;
    unsigned long long value = 0;
    size_t i;
    int fd = -1;

    (void)argc;
    (void)argv;

    for (i = 0; i < ARRAY_SIZE(candidates); i++) {
        fd = open_tracepoint_counter(candidates[i].group,
                                     candidates[i].event, &id);
        if (fd >= 0) {
            printf("tracepoint: %s:%s id=%llu\n", candidates[i].group,
                   candidates[i].event, id);
            break;
        }
    }

    if (fd < 0) {
        fprintf(stderr, "No usable perf tracepoint found: %s\n",
                strerror(errno));
        return 2;
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    workload_syscall(300);
    workload_cpu(100);
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    if (read_counter_value(fd, &value) != 0) {
        close(fd);
        return 1;
    }

    close(fd);
    printf("tracepoint count: %llu\n", (unsigned long long)value);
    return value > 0 ? 0 : 1;
}

static int wait_for_event_id(const char *group, const char *event,
                             unsigned long long *id)
{
    int i;

    for (i = 0; i < 50; i++) {
        if (tracefs_event_id(group, event, id) == 0)
            return 0;
        usleep(10000);
    }

    return -1;
}

static int mode_kprobe(int argc, char **argv)
{
    const char *tracefs = find_tracefs();
    char path[PATH_MAX];
    unsigned long long id = 0;
    unsigned long long value = 0;
    int fd;

    (void)argc;
    (void)argv;

    if (tracefs == NULL) {
        fprintf(stderr, "tracefs is not available\n");
        return 2;
    }

    snprintf(path, sizeof(path), "%s/kprobe_events", tracefs);
    write_string_file(path, "-:perf_demo_vfs_read\n");
    if (write_string_file(path, "p:perf_demo_vfs_read vfs_read\n") != 0) {
        fprintf(stderr, "cannot create kprobe: %s\n", strerror(errno));
        return 2;
    }

    if (wait_for_event_id("kprobes", "perf_demo_vfs_read", &id) != 0) {
        fprintf(stderr, "kprobe event did not appear\n");
        write_string_file(path, "-:perf_demo_vfs_read\n");
        return 2;
    }

    fd = open_tracepoint_counter("kprobes", "perf_demo_vfs_read", &id);
    if (fd < 0) {
        fprintf(stderr, "cannot open kprobe perf event: %s\n", strerror(errno));
        write_string_file(path, "-:perf_demo_vfs_read\n");
        return 2;
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    workload_file(400);
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    read_counter_value(fd, &value);
    close(fd);
    write_string_file(path, "-:perf_demo_vfs_read\n");

    printf("kprobe vfs_read count: %llu\n", (unsigned long long)value);
    return value > 0 ? 0 : 1;
}

__attribute__((noinline)) static void uprobe_target(unsigned long value)
{
    sink += value * 2654435761UL;
}

static int mode_uprobe(int argc, char **argv)
{
    const char *tracefs = find_tracefs();
    char self[PATH_MAX];
    char definition[PATH_MAX + 128];
    char path[PATH_MAX];
    unsigned long long offset = 0;
    unsigned long long id = 0;
    unsigned long long value = 0;
    uint64_t end;
    ssize_t length;
    int fd;

    (void)argc;
    (void)argv;

    if (tracefs == NULL) {
        fprintf(stderr, "tracefs is not available\n");
        return 2;
    }

    length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0) {
        fprintf(stderr, "cannot resolve /proc/self/exe: %s\n", strerror(errno));
        return 2;
    }
    self[length] = '\0';

    if (elf_find_symbol_offset(self, "uprobe_target", &offset) != 0) {
        fprintf(stderr, "uprobe_target symbol not found in %s\n", self);
        return 2;
    }

    snprintf(definition, sizeof(definition), "p:perf_demo_target %s:0x%llx\n",
             self, offset);
    snprintf(path, sizeof(path), "%s/uprobe_events", tracefs);
    write_string_file(path, "-:perf_demo_target\n");
    if (write_string_file(path, definition) != 0) {
        fprintf(stderr, "cannot create uprobe: %s\n", strerror(errno));
        return 2;
    }

    if (wait_for_event_id("uprobes", "perf_demo_target", &id) != 0) {
        fprintf(stderr, "uprobe event did not appear\n");
        write_string_file(path, "-:perf_demo_target\n");
        return 2;
    }

    fd = open_tracepoint_counter("uprobes", "perf_demo_target", &id);
    if (fd < 0) {
        fprintf(stderr, "cannot open uprobe perf event: %s\n", strerror(errno));
        write_string_file(path, "-:perf_demo_target\n");
        return 2;
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    end = monotonic_ns() + 300000000ULL;
    while (monotonic_ns() < end)
        uprobe_target(monotonic_ns());
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    read_counter_value(fd, &value);
    close(fd);
    write_string_file(path, "-:perf_demo_target\n");

    printf("uprobe uprobe_target count: %llu\n", (unsigned long long)value);
    return value > 0 ? 0 : 1;
}

static int mode_pid(int argc, char **argv)
{
    struct perf_event_attr attr;
    unsigned long long value = 0;
    char byte = 'x';
    int pipefd[2];
    pid_t child;
    int fd;
    int status;

    (void)argc;
    (void)argv;

    if (pipe(pipefd) != 0) {
        perror("pipe");
        return 1;
    }

    child = fork();
    if (child < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (child == 0) {
        close(pipefd[1]);
        if (read(pipefd[0], &byte, 1) != 1)
            _exit(1);
        close(pipefd[0]);
        workload_cpu(400);
        _exit(0);
    }

    close(pipefd[0]);

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_TASK_CLOCK;
    attr.disabled = 1;
    attr.exclude_hv = 1;

    fd = perf_event_open(&attr, child, -1, -1, 0);
    if (fd < 0) {
        fprintf(stderr, "perf_event_open for pid %d failed: %s\n", child,
                strerror(errno));
        close(pipefd[1]);
        kill(child, SIGKILL);
        waitpid(child, NULL, 0);
        return 2;
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    if (write(pipefd[1], &byte, 1) != 1)
        perror("write");
    close(pipefd[1]);
    waitpid(child, &status, 0);
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    read_counter_value(fd, &value);
    close(fd);

    printf("pid mode: child=%d task-clock=%llu\n", child,
           (unsigned long long)value);
    return value > 0 ? 0 : 1;
}

static int mode_cpu(int argc, char **argv)
{
    struct perf_event_attr attr;
    unsigned long long value = 0;
    cpu_set_t set;
    int fd;

    (void)argc;
    (void)argv;

    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        fprintf(stderr, "sched_setaffinity failed: %s\n", strerror(errno));

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.disabled = 1;
    attr.exclude_hv = 1;

    fd = perf_event_open(&attr, -1, 0, -1, 0);
    if (fd < 0) {
        fprintf(stderr, "system-wide perf_event_open failed: %s\n",
                strerror(errno));
        return 2;
    }

    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
    workload_cpu(300);
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

    read_counter_value(fd, &value);
    close(fd);

    printf("cpu mode: cpu0 cpu-clock=%llu\n", (unsigned long long)value);
    return value > 0 ? 0 : 1;
}

static void usage(const char *program)
{
    printf("Usage: %s <mode> [duration_ms]\n", program);
    printf("Modes:\n");
    printf("  stat [ms]       software/hardware counters and IPC\n");
    printf("  group           grouped counters and multiplexing scale\n");
    printf("  sample [ms]     mmap ring buffer sampling with callchains\n");
    printf("  tracepoint      perf tracepoint counter\n");
    printf("  kprobe          dynamic kprobe through tracefs plus perf\n");
    printf("  uprobe          dynamic uprobe on this binary plus perf\n");
    printf("  pid             count a forked child process only\n");
    printf("  cpu             system-wide count on CPU 0\n");
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        int (*run)(int argc, char **argv);
    } modes[] = {
        { "stat", mode_stat },       { "group", mode_group },
        { "sample", mode_sample },   { "tracepoint", mode_tracepoint },
        { "kprobe", mode_kprobe },   { "uprobe", mode_uprobe },
        { "pid", mode_pid },         { "cpu", mode_cpu },
    };
    size_t i;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }

    for (i = 0; i < ARRAY_SIZE(modes); i++) {
        if (strcmp(argv[1], modes[i].name) == 0)
            return modes[i].run(argc - 2, argv + 2);
    }

    usage(argv[0]);
    return 1;
}
