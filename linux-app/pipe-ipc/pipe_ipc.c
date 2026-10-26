#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int anon_pipe_demo(void)
{
    const char *message = "hello from pipe";
    int fds[2];
    pid_t pid;
    char buf[64];
    ssize_t n;
    int status = 0;

    if (pipe(fds) < 0) {
        perror("pipe");
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        close(fds[0]);
        if (write(fds[1], message, strlen(message)) != (ssize_t)strlen(message))
            _exit(1);
        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    n = read(fds[0], buf, sizeof(buf) - 1);
    close(fds[0]);
    if (n < 0) {
        perror("read");
        return -1;
    }
    buf[n] = '\0';

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

    printf("pipe: received \"%s\"\n", buf);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
    return strcmp(buf, message) == 0 ? 0 : -1;
}

static int fifo_demo(void)
{
    const char *message = "hello from fifo";
    char path[64];
    char buf[64];
    pid_t pid;
    ssize_t n;
    int fd;
    int status = 0;

    snprintf(path, sizeof(path), "/tmp/linux-app-fifo-%ld", (long)getpid());
    unlink(path);
    if (mkfifo(path, 0600) < 0) {
        perror("mkfifo");
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        unlink(path);
        return -1;
    }

    if (pid == 0) {
        fd = open(path, O_WRONLY);
        if (fd < 0)
            _exit(1);
        if (write(fd, message, strlen(message)) != (ssize_t)strlen(message))
            _exit(1);
        close(fd);
        _exit(0);
    }

    /* 命名 FIFO 的 open/read 与匿名管道相同，只是通过文件系统路径访问 */
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open fifo");
        unlink(path);
        return -1;
    }
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    unlink(path);
    if (n < 0) {
        perror("read fifo");
        return -1;
    }
    buf[n] = '\0';

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }

    printf("fifo: received \"%s\"\n", buf);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
    return strcmp(buf, message) == 0 ? 0 : -1;
}

int main(void)
{
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (anon_pipe_demo() != 0)
        failures++;
    if (fifo_demo() != 0)
        failures++;

    if (failures != 0) {
        fprintf(stderr, "pipe-ipc: FAIL\n");
        return EXIT_FAILURE;
    }

    printf("pipe-ipc: PASS\n");
    return EXIT_SUCCESS;
}
