#ifndef LINUX_APP_BPF_SYS_H
#define LINUX_APP_BPF_SYS_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static inline int
bpf_sys(enum bpf_cmd cmd, union bpf_attr *attr, unsigned int size)
{
    return (int)syscall(__NR_bpf, cmd, attr, size);
}

static inline int
bpf_map_create(enum bpf_map_type type, uint32_t key_size, uint32_t value_size,
               uint32_t max_entries, uint32_t flags)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_type = type;
    attr.key_size = key_size;
    attr.value_size = value_size;
    attr.max_entries = max_entries;
    attr.map_flags = flags;

    return bpf_sys(BPF_MAP_CREATE, &attr, sizeof(attr));
}

static inline int
bpf_map_update(int map_fd, const void *key, const void *value, uint64_t flags)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = map_fd;
    attr.key = (uint64_t)(uintptr_t)key;
    attr.value = (uint64_t)(uintptr_t)value;
    attr.flags = flags;

    return bpf_sys(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr));
}

static inline int
bpf_map_lookup(int map_fd, const void *key, void *value)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = map_fd;
    attr.key = (uint64_t)(uintptr_t)key;
    attr.value = (uint64_t)(uintptr_t)value;

    return bpf_sys(BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
}

static inline int
bpf_map_delete(int map_fd, const void *key)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = map_fd;
    attr.key = (uint64_t)(uintptr_t)key;

    return bpf_sys(BPF_MAP_DELETE_ELEM, &attr, sizeof(attr));
}

static inline int
bpf_map_get_next_key(int map_fd, const void *key, void *next_key)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.map_fd = map_fd;
    attr.key = (uint64_t)(uintptr_t)key;
    attr.next_key = (uint64_t)(uintptr_t)next_key;

    return bpf_sys(BPF_MAP_GET_NEXT_KEY, &attr, sizeof(attr));
}

static inline int
bpf_prog_load(enum bpf_prog_type type, const struct bpf_insn *insns,
              uint32_t count, const char *license, char *log,
              uint32_t log_size)
{
    union bpf_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.prog_type = type;
    attr.insn_cnt = count;
    attr.insns = (uint64_t)(uintptr_t)insns;
    attr.license = (uint64_t)(uintptr_t)license;
    if (log != NULL) {
        attr.log_buf = (uint64_t)(uintptr_t)log;
        attr.log_size = log_size;
        attr.log_level = 1;
    }

    return bpf_sys(BPF_PROG_LOAD, &attr, sizeof(attr));
}

static inline int
bpf_obj_get_info_by_fd(int fd, void *info, uint32_t *info_len)
{
    union bpf_attr attr;
    int rc;

    memset(&attr, 0, sizeof(attr));
    attr.info.bpf_fd = fd;
    attr.info.info_len = *info_len;
    attr.info.info = (uint64_t)(uintptr_t)info;

    rc = bpf_sys(BPF_OBJ_GET_INFO_BY_FD, &attr, sizeof(attr));
    if (rc == 0)
        *info_len = attr.info.info_len;

    return rc;
}

static inline const char *find_tracefs(void)
{
    static char tracefs[PATH_MAX];
    static const char *candidates[] = {
        "/sys/kernel/tracing",
        "/sys/kernel/debug/tracing",
    };
    char path[PATH_MAX];
    size_t i;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        snprintf(path, sizeof(path), "%s/available_tracers", candidates[i]);
        if (access(path, F_OK) == 0) {
            snprintf(tracefs, sizeof(tracefs), "%s", candidates[i]);
            return tracefs;
        }
    }

    return NULL;
}

static inline int
tracefs_path(char *path, size_t size, const char *relative)
{
    const char *root = find_tracefs();

    if (root == NULL)
        return -1;
    if (snprintf(path, size, "%s/%s", root, relative) >= (int)size)
        return -1;

    return 0;
}

static inline int
tracefs_write_file(const char *relative, const char *value)
{
    char path[PATH_MAX];
    size_t length = strlen(value);
    ssize_t written;
    int fd;

    if (tracefs_path(path, sizeof(path), relative) != 0)
        return -1;

    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    written = write(fd, value, length);
    close(fd);

    return written == (ssize_t)length ? 0 : -1;
}

static inline int
tracefs_append_file(const char *relative, const char *value)
{
    char path[PATH_MAX];
    size_t length = strlen(value);
    ssize_t written;
    int fd;

    if (tracefs_path(path, sizeof(path), relative) != 0)
        return -1;

    fd = open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0)
        return -1;
    written = write(fd, value, length);
    close(fd);

    return written == (ssize_t)length ? 0 : -1;
}

static inline int
tracefs_clear_file(const char *relative)
{
    char path[PATH_MAX];
    int fd;

    if (tracefs_path(path, sizeof(path), relative) != 0)
        return -1;

    fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0)
        return -1;
    close(fd);

    return 0;
}

static inline int
tracefs_read_file(const char *relative, char *buffer, size_t size)
{
    char path[PATH_MAX];
    ssize_t bytes;
    int fd;

    if (size == 0)
        return -1;
    if (tracefs_path(path, sizeof(path), relative) != 0)
        return -1;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    bytes = read(fd, buffer, size - 1);
    close(fd);

    if (bytes < 0)
        return -1;
    buffer[bytes] = '\0';
    return (int)bytes;
}

static inline int
tracefs_read_uint(const char *relative, unsigned long long *value)
{
    char buffer[64];
    char *end = NULL;

    if (tracefs_read_file(relative, buffer, sizeof(buffer)) < 0)
        return -1;

    errno = 0;
    *value = strtoull(buffer, &end, 10);
    if (errno != 0 || end == buffer)
        return -1;

    return 0;
}

static inline int
tracefs_event_id(const char *group, const char *event, unsigned long long *id)
{
    char relative[PATH_MAX];

    snprintf(relative, sizeof(relative), "events/%s/%s/id", group, event);
    return tracefs_read_uint(relative, id);
}

static inline int
perf_event_open_sys(struct perf_event_attr *attr, pid_t pid, int cpu,
                    int group_fd, unsigned long flags)
{
    return (int)syscall(__NR_perf_event_open, attr, pid, cpu, group_fd,
                        flags);
}

static inline int
tracepoint_perf_fd(unsigned long long id, pid_t pid, int cpu)
{
    struct perf_event_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_TRACEPOINT;
    attr.size = sizeof(attr);
    attr.config = id;
    attr.sample_period = 1;
    attr.wakeup_events = 1;
    attr.disabled = 1;

    return perf_event_open_sys(&attr, pid, cpu, -1, 0);
}

static inline int
attach_bpf_to_perf(int perf_fd, int prog_fd)
{
    if (ioctl(perf_fd, PERF_EVENT_IOC_SET_BPF, prog_fd) != 0)
        return -1;
    if (ioctl(perf_fd, PERF_EVENT_IOC_ENABLE, 0) != 0)
        return -1;

    return 0;
}

static inline void
detach_bpf_from_perf(int perf_fd)
{
    if (perf_fd >= 0) {
        ioctl(perf_fd, PERF_EVENT_IOC_DISABLE, 0);
        close(perf_fd);
    }
}

#endif
