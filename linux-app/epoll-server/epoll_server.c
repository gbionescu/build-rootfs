#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define MAX_EVENTS 16
#define BUFFER_SIZE 1024
#define CLIENT_RETRIES 50
#define RETRY_DELAY_MS 100

static int parse_port(const char *text, uint16_t *port)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 1 || value > 65535)
        return -1;

    *port = (uint16_t)value;
    return 0;
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_listener(const char *text)
{
    struct sockaddr_in addr;
    uint16_t port;
    int fd;
    int yes = 1;

    if (parse_port(text, &port) < 0) {
        fprintf(stderr, "invalid port: %s\n", text);
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 128) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    if (set_nonblocking(fd) < 0) {
        perror("fcntl");
        close(fd);
        return -1;
    }

    return fd;
}

static int run_server(const char *port)
{
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];
    char buf[BUFFER_SIZE];
    int listen_fd;
    int epfd;

    listen_fd = create_listener(port);
    if (listen_fd < 0)
        return EXIT_FAILURE;

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        perror("epoll_ctl");
        close(epfd);
        close(listen_fd);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "epoll server listening on 127.0.0.1:%s\n", port);

    for (;;) {
        int ready = epoll_wait(epfd, events, MAX_EVENTS, -1);

        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < ready; i++) {
            int fd = events[i].data.fd;

            if (fd == listen_fd) {
                for (;;) {
                    int client = accept4(listen_fd, NULL, NULL, SOCK_NONBLOCK);

                    if (client < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            break;
                        perror("accept4");
                        break;
                    }

                    memset(&ev, 0, sizeof(ev));
                    ev.events = EPOLLIN;
                    ev.data.fd = client;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client, &ev) < 0) {
                        perror("epoll_ctl add");
                        close(client);
                    } else {
                        fprintf(stderr, "server: accepted fd %d\n", client);
                    }
                }
            } else {
                ssize_t count = read(fd, buf, sizeof(buf));

                if (count > 0) {
                    ssize_t written = write(fd, buf, (size_t)count);

                    /* 演示使用小报文；部分写或缓冲区满时直接关闭连接 */
                    if (written != count) {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                    }
                } else if (count == 0) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                } else if (errno != EAGAIN && errno != EWOULDBLOCK &&
                           errno != EINTR) {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                }
            }
        }
    }

    close(epfd);
    close(listen_fd);
    return EXIT_SUCCESS;
}

static void sleep_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int connect_to(const char *text)
{
    struct sockaddr_in addr;
    uint16_t port;
    int attempt;
    int fd;

    if (parse_port(text, &port) < 0) {
        fprintf(stderr, "invalid port: %s\n", text);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    /* 服务器可能尚未就绪，带重试地连接 */
    for (attempt = 0; attempt < CLIENT_RETRIES; attempt++) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("socket");
            return -1;
        }
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return fd;
        close(fd);
        sleep_ms(RETRY_DELAY_MS);
    }

    fprintf(stderr, "client: could not connect to 127.0.0.1:%s\n", text);
    return -1;
}

static int run_client(const char *port)
{
    const char *message = "hello epoll";
    char buf[BUFFER_SIZE];
    ssize_t n;
    int fd;

    fd = connect_to(port);
    if (fd < 0)
        return EXIT_FAILURE;

    if (write(fd, message, strlen(message)) != (ssize_t)strlen(message)) {
        perror("write");
        close(fd);
        return EXIT_FAILURE;
    }

    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0) {
        perror("read");
        return EXIT_FAILURE;
    }
    buf[n] = '\0';

    printf("client: sent \"%s\", received \"%s\"\n", message, buf);
    if (strcmp(message, buf) != 0) {
        fprintf(stderr, "epoll: FAIL\n");
        return EXIT_FAILURE;
    }

    printf("epoll: PASS\n");
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGPIPE, SIG_IGN);

    if (argc != 3) {
        fprintf(stderr, "usage: %s <server|client> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "server") == 0)
        return run_server(argv[2]);
    if (strcmp(argv[1], "client") == 0)
        return run_client(argv[2]);

    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return EXIT_FAILURE;
}
