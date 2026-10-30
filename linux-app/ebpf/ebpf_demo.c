#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../common/elf_symbol.h"
#include "bpf_insn.h"
#include "bpf_sys.h"

#define FILE_BUFFER_SIZE (64 * 1024)
#define FILE_ROUNDS 8
#define SCHED_CHILDREN 4
#define SCHED_ROUNDS 200
#define RING_PAGES 8
#define MAX_CPUS 256
#define MAX_INSNS 64
#define VERIFIER_LOG_SIZE (64 * 1024)

static volatile unsigned long sink;

struct output_data {
    char comm[16];
    uint64_t pid;
};

struct perf_output {
    int fd;
    void *map;
    size_t map_size;
};

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

__attribute__((noinline)) void ebpf_target(unsigned long value)
{
    sink += value * 2654435761UL;
}

static void workload_file(void)
{
    char path[] = "/tmp/ebpf-demo-XXXXXX";
    char *buffer = malloc(FILE_BUFFER_SIZE);
    int fd;
    int i;

    if (buffer == NULL)
        return;

    for (i = 0; i < FILE_BUFFER_SIZE; i++)
        buffer[i] = (char)('A' + i % 26);

    fd = mkstemp(path);
    if (fd < 0) {
        free(buffer);
        return;
    }

    for (i = 0; i < FILE_ROUNDS; i++) {
        if (write(fd, buffer, FILE_BUFFER_SIZE) != FILE_BUFFER_SIZE)
            break;
    }

    if (lseek(fd, 0, SEEK_SET) == 0) {
        for (i = 0; i < FILE_ROUNDS; i++) {
            if (read(fd, buffer, FILE_BUFFER_SIZE) <= 0)
                break;
        }
    }

    close(fd);
    unlink(path);
    free(buffer);
}

static void workload_sched(void)
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

static void workload_target(void)
{
    uint64_t end = monotonic_ns() + 300000000ULL;

    while (monotonic_ns() < end)
        ebpf_target((unsigned long)monotonic_ns());
}

static int bpf_error_rc(void)
{
    if (errno == EPERM || errno == EACCES || errno == ENOSYS)
        return 2;
    return 1;
}

static int build_simple_prog(struct bpf_insn *insns, int *count, int value)
{
    int n = 0;

    insns[n++] = BPF_MOV64_IMM(BPF_REG_0, value);
    insns[n++] = BPF_EXIT_INSN();
    *count = n;
    return 0;
}

static int build_counter_prog(struct bpf_insn *insns, int *count, int map_fd,
                              int return_value)
{
    int n = 0;

    insns[n++] = BPF_ST_MEM(BPF_W, BPF_REG_10, -4, 0);
    emit_ld_map_fd(insns, &n, BPF_REG_1, map_fd);
    insns[n++] = BPF_MOV64_REG(BPF_REG_2, BPF_REG_10);
    insns[n++] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4);
    insns[n++] = BPF_CALL_FUNC(BPF_FUNC_map_lookup_elem);
    insns[n++] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2);
    insns[n++] = BPF_MOV64_IMM(BPF_REG_1, 1);
    insns[n++] = BPF_STX_XADD(BPF_DW, BPF_REG_0, BPF_REG_1, 0);
    insns[n++] = BPF_MOV64_IMM(BPF_REG_0, return_value);
    insns[n++] = BPF_EXIT_INSN();
    *count = n;
    return 0;
}

static int build_printk_prog(struct bpf_insn *insns, int *count)
{
    int n = 0;

    emit_ld_imm64(insns, &n, BPF_REG_1, 0x000a642520746968ULL);
    insns[n++] = BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_1, -8);
    insns[n++] = BPF_CALL_FUNC(BPF_FUNC_get_current_pid_tgid);
    insns[n++] = BPF_MOV64_REG(BPF_REG_3, BPF_REG_0);
    insns[n++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_10);
    insns[n++] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8);
    insns[n++] = BPF_MOV64_IMM(BPF_REG_2, 8);
    insns[n++] = BPF_CALL_FUNC(BPF_FUNC_trace_printk);
    insns[n++] = BPF_MOV64_IMM(BPF_REG_0, 0);
    insns[n++] = BPF_EXIT_INSN();
    *count = n;
    return 0;
}

static int build_perf_output_prog(struct bpf_insn *insns, int *count,
                                  int map_fd)
{
    int n = 0;

    insns[n++] = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
    insns[n++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_10);
    insns[n++] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -24);
    insns[n++] = BPF_MOV64_IMM(BPF_REG_2, 16);
    insns[n++] = BPF_CALL_FUNC(BPF_FUNC_get_current_comm);
    insns[n++] = BPF_CALL_FUNC(BPF_FUNC_get_current_pid_tgid);
    insns[n++] = BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_0, -8);
    emit_ld_map_fd(insns, &n, BPF_REG_2, map_fd);
    insns[n++] = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
    emit_ld_imm64(insns, &n, BPF_REG_3, BPF_F_CURRENT_CPU);
    insns[n++] = BPF_MOV64_REG(BPF_REG_4, BPF_REG_10);
    insns[n++] = BPF_ALU64_IMM(BPF_ADD, BPF_REG_4, -24);
    insns[n++] = BPF_MOV64_IMM(BPF_REG_5, 24);
    insns[n++] = BPF_CALL_FUNC(BPF_FUNC_perf_event_output);
    insns[n++] = BPF_MOV64_IMM(BPF_REG_0, 0);
    insns[n++] = BPF_EXIT_INSN();
    *count = n;
    return 0;
}

static int build_invalid_prog(struct bpf_insn *insns, int *count)
{
    int n = 0;

    insns[n++] = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_10, 8);
    insns[n++] = BPF_EXIT_INSN();
    *count = n;
    return 0;
}

static int load_prog(enum bpf_prog_type type, struct bpf_insn *insns, int count,
                     const char *name)
{
    char log[VERIFIER_LOG_SIZE];
    int fd;

    memset(log, 0, sizeof(log));
    fd = bpf_prog_load(type, insns, (uint32_t)count, "GPL", log,
                       sizeof(log));
    if (fd < 0) {
        int saved = errno;

        fprintf(stderr, "%s: bpf_prog_load failed: %s\n", name,
                strerror(saved));
        if (log[0] != '\0')
            fprintf(stderr, "%s\n", log);
        errno = saved;
    }

    return fd;
}

static int wait_event_id(const char *group, const char *event)
{
    unsigned long long id;
    int i;

    for (i = 0; i < 100; i++) {
        if (tracefs_event_id(group, event, &id) == 0)
            return 0;
        usleep(10000);
    }

    return -1;
}

static int create_dynamic_event(const char *kind, const char *name,
                                const char *definition)
{
    char relative[PATH_MAX];
    char remove[PATH_MAX];
    const char *group;

    snprintf(relative, sizeof(relative), "%s_events", kind);
    snprintf(remove, sizeof(remove), "-:%s", name);
    tracefs_write_file(relative, remove);

    if (tracefs_append_file(relative, definition) != 0)
        return -1;

    group = strcmp(kind, "uprobe") == 0 ? "uprobes" : "kprobes";
    return wait_event_id(group, name);
}

static void remove_dynamic_event(const char *kind, const char *name)
{
    char relative[PATH_MAX];
    char remove[PATH_MAX];

    snprintf(relative, sizeof(relative), "%s_events", kind);
    snprintf(remove, sizeof(remove), "-:%s", name);
    tracefs_write_file(relative, remove);
}

static int attach_trace_program(const char *group, const char *event,
                                pid_t pid, int cpu, int prog_fd,
                                int *perf_fd_out)
{
    unsigned long long id;
    int perf_fd;

    if (tracefs_event_id(group, event, &id) != 0)
        return -1;

    perf_fd = tracepoint_perf_fd(id, pid, cpu);
    if (perf_fd < 0)
        return -1;

    if (attach_bpf_to_perf(perf_fd, prog_fd) != 0) {
        int saved = errno;

        close(perf_fd);
        errno = saved;
        return -1;
    }

    *perf_fd_out = perf_fd;
    return 0;
}

static int run_counter_demo(const char *event_group, const char *event_name,
                            const char *dynamic_kind,
                            const char *dynamic_name,
                            const char *dynamic_definition, pid_t perf_pid,
                            int perf_cpu, enum bpf_prog_type prog_type,
                            void (*workload)(void))
{
    struct bpf_insn insns[MAX_INSNS];
    char definition[PATH_MAX + 128] = "";
    int map_fd = -1;
    int prog_fd = -1;
    int perf_fd = -1;
    int created = 0;
    int rc = 1;
    int count;
    uint32_t key = 0;
    uint64_t value = 0;

    if (dynamic_definition != NULL)
        snprintf(definition, sizeof(definition), "%s", dynamic_definition);

    map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                            sizeof(uint64_t), 1, 0);
    if (map_fd < 0) {
        fprintf(stderr, "map create failed: %s\n", strerror(errno));
        return bpf_error_rc();
    }

    build_counter_prog(insns, &count, map_fd, 0);
    prog_fd = load_prog(prog_type, insns, count, dynamic_name);
    if (prog_fd < 0) {
        rc = bpf_error_rc();
        goto out;
    }

    if (dynamic_kind != NULL) {
        if (create_dynamic_event(dynamic_kind, dynamic_name, definition) != 0) {
            fprintf(stderr, "cannot create %s event: %s\n", dynamic_kind,
                    strerror(errno));
            rc = 2;
            goto out;
        }
        created = 1;
    }

    if (attach_trace_program(event_group, event_name, perf_pid, perf_cpu,
                             prog_fd, &perf_fd) != 0) {
        fprintf(stderr, "cannot attach BPF program: %s\n", strerror(errno));
        rc = bpf_error_rc();
        goto out;
    }

    workload();

    detach_bpf_from_perf(perf_fd);
    perf_fd = -1;

    if (bpf_map_lookup(map_fd, &key, &value) != 0) {
        fprintf(stderr, "map lookup failed: %s\n", strerror(errno));
        rc = 1;
        goto out;
    }

    printf("%s count: %llu\n", event_name, (unsigned long long)value);
    rc = value > 0 ? 0 : 1;

out:
    if (perf_fd >= 0)
        detach_bpf_from_perf(perf_fd);
    if (created)
        remove_dynamic_event(dynamic_kind, dynamic_name);
    if (prog_fd >= 0)
        close(prog_fd);
    if (map_fd >= 0)
        close(map_fd);
    return rc;
}

static int udp_receiver(struct sockaddr_in *addr, socklen_t *addrlen)
{
    struct sockaddr_in local;
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;

    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
        close(fd);
        return -1;
    }

    *addrlen = sizeof(*addr);
    if (getsockname(fd, (struct sockaddr *)addr, addrlen) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int attach_socket_filter(int sock, int prog_fd)
{
    return setsockopt(sock, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd,
                      sizeof(prog_fd));
}

static int send_udp(const struct sockaddr_in *addr, socklen_t addrlen,
                    const char *payload)
{
    int fd;
    ssize_t bytes;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    bytes = sendto(fd, payload, strlen(payload), 0,
                   (const struct sockaddr *)addr, addrlen);
    close(fd);
    return bytes == (ssize_t)strlen(payload) ? 0 : -1;
}

static int recv_with_timeout(int fd, void *buffer, size_t size, int timeout_ms)
{
    struct pollfd pfd;

    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    if (poll(&pfd, 1, timeout_ms) <= 0)
        return 0;

    return (int)recv(fd, buffer, size, 0);
}

static void print_prog_info(int prog_fd)
{
    struct bpf_prog_info info;
    uint32_t length = sizeof(info);
    unsigned int i;

    memset(&info, 0, sizeof(info));
    if (bpf_obj_get_info_by_fd(prog_fd, &info, &length) != 0)
        return;

    printf("program id=%u tag=", info.id);
    for (i = 0; i < 8; i++)
        printf("%02x", info.tag[i]);
    printf(" jited_bytes=%u\n", info.jited_prog_len);
}

static int mode_map(void)
{
    int hash_fd;
    int array_fd;
    uint32_t key;
    uint32_t next;
    uint64_t value;
    uint64_t hash_value = 0;
    uint64_t array_value = 0;
    int i;
    int count;

    hash_fd = bpf_map_create(BPF_MAP_TYPE_HASH, sizeof(uint32_t),
                             sizeof(uint64_t), 8, 0);
    if (hash_fd < 0) {
        fprintf(stderr, "hash map create failed: %s\n", strerror(errno));
        return bpf_error_rc();
    }

    array_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                              sizeof(uint64_t), 4, 0);
    if (array_fd < 0) {
        fprintf(stderr, "array map create failed: %s\n", strerror(errno));
        close(hash_fd);
        return bpf_error_rc();
    }

    for (i = 0; i < 4; i++) {
        key = (uint32_t)i;
        value = 100 + (uint64_t)i;
        if (bpf_map_update(hash_fd, &key, &value, BPF_ANY) != 0) {
            fprintf(stderr, "hash update failed: %s\n", strerror(errno));
            close(array_fd);
            close(hash_fd);
            return 1;
        }
    }

    key = 2;
    value = 0;
    if (bpf_map_lookup(hash_fd, &key, &value) != 0 || value != 102) {
        fprintf(stderr, "hash lookup failed\n");
        close(array_fd);
        close(hash_fd);
        return 1;
    }
    hash_value = value;

    count = 0;
    key = 0;
    while (bpf_map_get_next_key(hash_fd, count == 0 ? NULL : &key, &next) ==
           0) {
        key = next;
        count++;
        if (count > 16)
            break;
    }

    key = 1;
    bpf_map_delete(hash_fd, &key);
    if (bpf_map_lookup(hash_fd, &key, &value) == 0 || errno != ENOENT) {
        fprintf(stderr, "hash delete verification failed\n");
        close(array_fd);
        close(hash_fd);
        return 1;
    }

    key = 3;
    value = 333;
    if (bpf_map_update(array_fd, &key, &value, BPF_ANY) != 0) {
        fprintf(stderr, "array update failed: %s\n", strerror(errno));
        close(array_fd);
        close(hash_fd);
        return 1;
    }

    value = 0;
    if (bpf_map_lookup(array_fd, &key, &value) != 0 || value != 333) {
        fprintf(stderr, "array lookup failed\n");
        close(array_fd);
        close(hash_fd);
        return 1;
    }
    array_value = value;

    key = 4;
    value = 444;
    if (bpf_map_update(array_fd, &key, &value, BPF_ANY) == 0) {
        fprintf(stderr, "array out-of-range update unexpectedly succeeded\n");
        close(array_fd);
        close(hash_fd);
        return 1;
    }

    printf("hash entries=%d, key2=%llu, delete key1=ok\n", count,
           (unsigned long long)hash_value);
    printf("array key3=%llu, out-of-range key4 rejected (%s)\n",
           (unsigned long long)array_value, strerror(errno));

    close(array_fd);
    close(hash_fd);
    return 0;
}

static int mode_socket(void)
{
    struct bpf_insn insns[MAX_INSNS];
    struct sockaddr_in addr;
    socklen_t addrlen;
    char buffer[64];
    int accept_fd;
    int drop_fd;
    int recv_fd;
    int count;
    int rc = 0;
    ssize_t bytes;

    build_simple_prog(insns, &count, 0xffff);
    accept_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, insns, count,
                          "socket-accept");
    if (accept_fd < 0)
        return bpf_error_rc();

    build_simple_prog(insns, &count, 0);
    drop_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, insns, count,
                        "socket-drop");
    if (drop_fd < 0) {
        close(accept_fd);
        return bpf_error_rc();
    }

    recv_fd = udp_receiver(&addr, &addrlen);
    if (recv_fd < 0) {
        perror("udp_receiver");
        close(drop_fd);
        close(accept_fd);
        return 1;
    }

    if (attach_socket_filter(recv_fd, accept_fd) != 0) {
        perror("SO_ATTACH_BPF accept");
        close(recv_fd);
        close(drop_fd);
        close(accept_fd);
        return 1;
    }

    if (send_udp(&addr, addrlen, "bpf-accept") != 0) {
        perror("send accept");
        rc = 1;
        goto out_accept;
    }

    memset(buffer, 0, sizeof(buffer));
    bytes = recv_with_timeout(recv_fd, buffer, sizeof(buffer) - 1, 500);
    if (bytes <= 0 || strcmp(buffer, "bpf-accept") != 0) {
        fprintf(stderr, "accept filter did not deliver packet\n");
        rc = 1;
        goto out_accept;
    }

    print_prog_info(accept_fd);

out_accept:
    close(recv_fd);
    close(drop_fd);
    close(accept_fd);

    if (rc != 0)
        return rc;

    recv_fd = udp_receiver(&addr, &addrlen);
    if (recv_fd < 0) {
        perror("udp_receiver drop");
        return 1;
    }

    build_simple_prog(insns, &count, 0);
    drop_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, insns, count,
                        "socket-drop-2");
    if (drop_fd < 0) {
        close(recv_fd);
        return bpf_error_rc();
    }

    if (attach_socket_filter(recv_fd, drop_fd) != 0) {
        perror("SO_ATTACH_BPF drop");
        close(drop_fd);
        close(recv_fd);
        return 1;
    }

    if (send_udp(&addr, addrlen, "bpf-drop") != 0) {
        perror("send drop");
        close(drop_fd);
        close(recv_fd);
        return 1;
    }

    bytes = recv_with_timeout(recv_fd, buffer, sizeof(buffer) - 1, 300);
    if (bytes > 0) {
        fprintf(stderr, "drop filter delivered a packet\n");
        rc = 1;
    } else {
        printf("socket filter accept/drop behavior verified\n");
    }

    close(drop_fd);
    close(recv_fd);
    return rc;
}

static int mode_counter(void)
{
    struct bpf_insn insns[MAX_INSNS];
    struct sockaddr_in addr;
    socklen_t addrlen;
    char buffer[64];
    int map_fd;
    int prog_fd;
    int recv_fd;
    int count;
    int i;
    uint32_t key = 0;
    uint64_t value = 0;

    map_fd = bpf_map_create(BPF_MAP_TYPE_ARRAY, sizeof(uint32_t),
                            sizeof(uint64_t), 1, 0);
    if (map_fd < 0) {
        fprintf(stderr, "counter map create failed: %s\n", strerror(errno));
        return bpf_error_rc();
    }

    build_counter_prog(insns, &count, map_fd, 0xffff);
    prog_fd = load_prog(BPF_PROG_TYPE_SOCKET_FILTER, insns, count,
                        "socket-counter");
    if (prog_fd < 0) {
        close(map_fd);
        return bpf_error_rc();
    }

    recv_fd = udp_receiver(&addr, &addrlen);
    if (recv_fd < 0) {
        perror("udp_receiver counter");
        close(prog_fd);
        close(map_fd);
        return 1;
    }

    if (attach_socket_filter(recv_fd, prog_fd) != 0) {
        perror("SO_ATTACH_BPF counter");
        close(recv_fd);
        close(prog_fd);
        close(map_fd);
        return 1;
    }

    for (i = 0; i < 10; i++) {
        if (send_udp(&addr, addrlen, "counter") != 0)
            break;
        recv_with_timeout(recv_fd, buffer, sizeof(buffer), 100);
    }

    usleep(100000);

    if (bpf_map_lookup(map_fd, &key, &value) != 0) {
        fprintf(stderr, "counter lookup failed: %s\n", strerror(errno));
        close(recv_fd);
        close(prog_fd);
        close(map_fd);
        return 1;
    }

    printf("socket filter map counter: %llu\n", (unsigned long long)value);
    close(recv_fd);
    close(prog_fd);
    close(map_fd);
    return value == 10 ? 0 : 1;
}

static int mode_kprobe(void)
{
    char definition[PATH_MAX];

    snprintf(definition, sizeof(definition),
             "p:ebpf_demo_vfs_read vfs_read\n");

    return run_counter_demo("kprobes", "ebpf_demo_vfs_read", "kprobe",
                            "ebpf_demo_vfs_read", definition, 0, -1,
                            BPF_PROG_TYPE_KPROBE, workload_file);
}

static int mode_tracepoint(void)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        perror("sched_setaffinity");

    return run_counter_demo("sched", "sched_switch", NULL, "sched_switch",
                            NULL, -1, 0, BPF_PROG_TYPE_TRACEPOINT,
                            workload_sched);
}

static int mode_uprobe(void)
{
    char self[PATH_MAX];
    char definition[PATH_MAX + 128];
    unsigned long long offset = 0;
    ssize_t length;

    length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0) {
        perror("/proc/self/exe");
        return 2;
    }
    self[length] = '\0';

    if (elf_find_symbol_offset(self, "ebpf_target", &offset) != 0) {
        fprintf(stderr, "ebpf_target symbol not found in %s\n", self);
        return 2;
    }

    snprintf(definition, sizeof(definition), "p:ebpf_demo_target %s:0x%llx\n",
             self, offset);

    return run_counter_demo("uprobes", "ebpf_demo_target", "uprobe",
                            "ebpf_demo_target", definition, 0, -1,
                            BPF_PROG_TYPE_KPROBE, workload_target);
}

static int mode_trace_printk(void)
{
    struct bpf_insn insns[MAX_INSNS];
    char definition[PATH_MAX];
    char *trace = NULL;
    unsigned long long old_tracing_on = 0;
    char value[32];
    int prog_fd = -1;
    int perf_fd = -1;
    int created = 0;
    int count;
    int rc = 1;

    snprintf(definition, sizeof(definition),
             "p:ebpf_demo_printk vfs_read\n");

    if (find_tracefs() == NULL) {
        fprintf(stderr, "tracefs is not available\n");
        return 2;
    }

    build_printk_prog(insns, &count);
    prog_fd = load_prog(BPF_PROG_TYPE_KPROBE, insns, count, "trace-printk");
    if (prog_fd < 0)
        return bpf_error_rc();

    if (create_dynamic_event("kprobe", "ebpf_demo_printk", definition) != 0) {
        fprintf(stderr, "cannot create printk kprobe: %s\n", strerror(errno));
        rc = 2;
        goto out;
    }
    created = 1;

    if (attach_trace_program("kprobes", "ebpf_demo_printk", 0, -1, prog_fd,
                             &perf_fd) != 0) {
        fprintf(stderr, "cannot attach printk program: %s\n", strerror(errno));
        rc = bpf_error_rc();
        goto out;
    }

    tracefs_read_uint("tracing_on", &old_tracing_on);
    tracefs_clear_file("trace");
    tracefs_write_file("tracing_on", "1");

    workload_file();

    detach_bpf_from_perf(perf_fd);
    perf_fd = -1;
    snprintf(value, sizeof(value), "%llu", old_tracing_on);
    tracefs_write_file("tracing_on", value);

    trace = malloc(1 << 20);
    if (trace == NULL) {
        rc = 1;
        goto out;
    }

    if (tracefs_read_file("trace", trace, 1 << 20) < 0) {
        fprintf(stderr, "cannot read trace buffer\n");
        rc = 1;
        goto out;
    }

    if (strstr(trace, "hit") != NULL) {
        char *line = trace;

        while (line != NULL && *line != '\0') {
            char *end = strchr(line, '\n');

            if (end != NULL)
                *end = '\0';
            if (strstr(line, "hit") != NULL) {
                printf("bpf_trace_printk: %s\n", line);
                break;
            }
            if (end == NULL)
                break;
            line = end + 1;
        }
        rc = 0;
    } else {
        fprintf(stderr, "bpf_trace_printk output not found\n");
        rc = 1;
    }

out:
    free(trace);
    if (perf_fd >= 0)
        detach_bpf_from_perf(perf_fd);
    if (created)
        remove_dynamic_event("kprobe", "ebpf_demo_printk");
    if (prog_fd >= 0)
        close(prog_fd);
    return rc;
}

static int parse_perf_output(struct perf_output *outputs, int count)
{
    int samples = 0;
    int i;

    for (i = 0; i < count; i++) {
        struct perf_event_mmap_page *meta = outputs[i].map;
        char *data;
        size_t data_size;
        uint64_t head;
        uint64_t tail;

        if (meta == NULL)
            continue;

        data = (char *)meta + getpagesize();
        data_size = outputs[i].map_size - (size_t)getpagesize();
        head = meta->data_head;
        __sync_synchronize();
        tail = meta->data_tail;

        while (tail < head) {
            struct perf_event_header *header =
                (struct perf_event_header *)(data + (tail % data_size));

            if (header->size < sizeof(*header) ||
                header->size > data_size)
                break;

            if (header->type == PERF_RECORD_SAMPLE) {
                unsigned char *cursor =
                    (unsigned char *)header + sizeof(*header);
                uint32_t raw_size;

                memcpy(&raw_size, cursor, sizeof(raw_size));
                cursor += sizeof(raw_size);
                if (raw_size >= sizeof(struct output_data)) {
                    struct output_data sample;

                    memcpy(&sample, cursor, sizeof(sample));
                    printf("perf output: comm=%s pid=%u\n", sample.comm,
                           (unsigned int)sample.pid);
                    samples++;
                }
            }

            tail += header->size;
        }

        meta->data_tail = head;
    }

    return samples;
}

static int mode_perf_output(void)
{
    struct bpf_insn insns[MAX_INSNS];
    struct perf_event_attr attr;
    struct perf_output outputs[MAX_CPUS];
    cpu_set_t set;
    char definition[PATH_MAX];
    long page_size;
    long cpu_count;
    size_t mmap_size;
    int map_fd = -1;
    int prog_fd = -1;
    int perf_fd = -1;
    int created = 0;
    int opened = 0;
    int count;
    int i;
    int rc = 1;
    uint32_t key;
    uint32_t value;
    int samples;

    memset(outputs, 0, sizeof(outputs));
    for (i = 0; i < MAX_CPUS; i++)
        outputs[i].fd = -1;

    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        perror("sched_setaffinity");

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        return 1;
    mmap_size = ((size_t)RING_PAGES + 1) * (size_t)page_size;

    cpu_count = sysconf(_SC_NPROCESSORS_CONF);
    if (cpu_count < 1)
        cpu_count = 1;
    if (cpu_count > MAX_CPUS)
        cpu_count = MAX_CPUS;

    map_fd = bpf_map_create(BPF_MAP_TYPE_PERF_EVENT_ARRAY, sizeof(uint32_t),
                            sizeof(uint32_t), (uint32_t)cpu_count, 0);
    if (map_fd < 0) {
        fprintf(stderr, "perf event array create failed: %s\n",
                strerror(errno));
        return bpf_error_rc();
    }

    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_SOFTWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_SW_BPF_OUTPUT;
    attr.sample_type = PERF_SAMPLE_RAW;
    attr.sample_period = 1;
    attr.wakeup_events = 1;
    attr.disabled = 1;

    for (i = 0; i < cpu_count; i++) {
        int fd = perf_event_open_sys(&attr, -1, i, -1, 0);

        if (fd < 0)
            continue;

        outputs[i].map = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
        if (outputs[i].map == MAP_FAILED) {
            outputs[i].map = NULL;
            close(fd);
            continue;
        }

        outputs[i].fd = fd;
        outputs[i].map_size = mmap_size;
        key = (uint32_t)i;
        value = (uint32_t)fd;
        if (bpf_map_update(map_fd, &key, &value, BPF_ANY) != 0) {
            munmap(outputs[i].map, mmap_size);
            outputs[i].map = NULL;
            close(fd);
            outputs[i].fd = -1;
            continue;
        }
        opened++;
    }

    if (opened == 0) {
        fprintf(stderr, "no BPF output perf events available\n");
        rc = 2;
        goto out;
    }

    build_perf_output_prog(insns, &count, map_fd);
    prog_fd = load_prog(BPF_PROG_TYPE_KPROBE, insns, count, "perf-output");
    if (prog_fd < 0) {
        rc = bpf_error_rc();
        goto out;
    }

    snprintf(definition, sizeof(definition),
             "p:ebpf_demo_output vfs_read\n");
    if (create_dynamic_event("kprobe", "ebpf_demo_output", definition) != 0) {
        fprintf(stderr, "cannot create output kprobe: %s\n", strerror(errno));
        rc = 2;
        goto out;
    }
    created = 1;

    if (attach_trace_program("kprobes", "ebpf_demo_output", 0, -1, prog_fd,
                             &perf_fd) != 0) {
        fprintf(stderr, "cannot attach output program: %s\n", strerror(errno));
        rc = bpf_error_rc();
        goto out;
    }

    for (i = 0; i < cpu_count; i++) {
        if (outputs[i].fd >= 0)
            ioctl(outputs[i].fd, PERF_EVENT_IOC_ENABLE, 0);
    }

    workload_file();

    detach_bpf_from_perf(perf_fd);
    perf_fd = -1;

    for (i = 0; i < cpu_count; i++) {
        if (outputs[i].fd >= 0)
            ioctl(outputs[i].fd, PERF_EVENT_IOC_DISABLE, 0);
    }

    samples = parse_perf_output(outputs, (int)cpu_count);
    printf("perf output samples: %d\n", samples);
    rc = samples > 0 ? 0 : 1;

out:
    if (perf_fd >= 0)
        detach_bpf_from_perf(perf_fd);
    if (created)
        remove_dynamic_event("kprobe", "ebpf_demo_output");
    for (i = 0; i < MAX_CPUS; i++) {
        if (outputs[i].map != NULL)
            munmap(outputs[i].map, outputs[i].map_size);
        if (outputs[i].fd >= 0)
            close(outputs[i].fd);
    }
    if (prog_fd >= 0)
        close(prog_fd);
    if (map_fd >= 0)
        close(map_fd);
    return rc;
}

static int mode_verifier(void)
{
    struct bpf_insn insns[MAX_INSNS];
    char log[VERIFIER_LOG_SIZE];
    int saved_errno;
    int count;
    int fd;

    build_invalid_prog(insns, &count);
    memset(log, 0, sizeof(log));
    fd = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, insns, (uint32_t)count,
                       "GPL", log, sizeof(log));
    saved_errno = errno;
    if (fd >= 0) {
        close(fd);
        fprintf(stderr, "invalid program unexpectedly loaded\n");
        return 1;
    }

    if (log[0] != '\0') {
        printf("verifier log:\n%.4000s\n", log);
        if (strstr(log, "invalid") != NULL || strstr(log, "R10") != NULL ||
            strstr(log, "stack") != NULL)
            return 0;
    }

    if (saved_errno == EPERM || saved_errno == EACCES ||
        saved_errno == ENOSYS) {
        fprintf(stderr, "BPF verifier unavailable: %s\n",
                strerror(saved_errno));
        return 2;
    }

    fprintf(stderr, "program was rejected but log did not explain why: %s\n",
            strerror(saved_errno));
    return 1;
}

static void usage(const char *program)
{
    printf("Usage: %s <mode>\n", program);
    printf("Modes:\n");
    printf("  map           array/hash map operations\n");
    printf("  socket        socket filter accept/drop and program info\n");
    printf("  counter       socket filter with map counter\n");
    printf("  kprobe        BPF kprobe counter on vfs_read\n");
    printf("  tracepoint    BPF tracepoint counter on sched_switch\n");
    printf("  uprobe        BPF uprobe counter on ebpf_target\n");
    printf("  trace-printk  bpf_trace_printk output\n");
    printf("  perf-output   bpf_perf_event_output samples\n");
    printf("  verifier      invalid program and verifier log\n");
}

int main(int argc, char **argv)
{
    static const struct {
        const char *name;
        int (*run)(void);
    } modes[] = {
        { "map", mode_map },
        { "socket", mode_socket },
        { "counter", mode_counter },
        { "kprobe", mode_kprobe },
        { "tracepoint", mode_tracepoint },
        { "uprobe", mode_uprobe },
        { "trace-printk", mode_trace_printk },
        { "perf-output", mode_perf_output },
        { "verifier", mode_verifier },
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

    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (strcmp(argv[1], modes[i].name) == 0)
            return modes[i].run();
    }

    usage(argv[0]);
    return 1;
}
