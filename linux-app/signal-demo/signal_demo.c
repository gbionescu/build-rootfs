#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t got_usr1;

static void on_usr1(int signo)
{
    (void)signo;
    got_usr1 = 1;
}

int main(void)
{
    struct sigaction sa;
    sigset_t set;
    int signo = 0;
    int rc;

    /* sigaction 注册异步信号处理函数 */
    sa.sa_handler = on_usr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    if (raise(SIGUSR1) != 0) {
        perror("raise");
        return EXIT_FAILURE;
    }

    if (!got_usr1) {
        fprintf(stderr, "signal: handler did not run\n");
        return EXIT_FAILURE;
    }
    printf("sigaction: handler ran for SIGUSR1\n");

    /* 阻塞 SIGUSR2 后 raise，信号保持 pending，用 sigwait 同步取走 */
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    if (sigprocmask(SIG_BLOCK, &set, NULL) < 0) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    if (raise(SIGUSR2) != 0) {
        perror("raise");
        return EXIT_FAILURE;
    }

    rc = sigwait(&set, &signo);
    if (rc != 0 || signo != SIGUSR2) {
        fprintf(stderr, "signal: sigwait failed\n");
        return EXIT_FAILURE;
    }
    printf("sigwait: received SIGUSR2\n");

    printf("signal: PASS\n");
    return EXIT_SUCCESS;
}
