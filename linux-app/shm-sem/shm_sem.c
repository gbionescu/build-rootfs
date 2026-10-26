#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

struct shared_data {
    sem_t ready;
    int value;
    char message[64];
};

int main(void)
{
    char name[64];
    char expected[64];
    struct shared_data *shm;
    pid_t pid;
    int fd;
    int status = 0;
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(name, sizeof(name), "/linux-app-shm-%ld", (long)getpid());

    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        perror("shm_open");
        return EXIT_FAILURE;
    }

    if (ftruncate(fd, sizeof(*shm)) < 0) {
        perror("ftruncate");
        shm_unlink(name);
        return EXIT_FAILURE;
    }

    shm = mmap(NULL, sizeof(*shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        close(fd);
        shm_unlink(name);
        return EXIT_FAILURE;
    }
    close(fd);

    /* pshared=1 让信号量可以跨进程使用 */
    if (sem_init(&shm->ready, 1, 0) < 0) {
        perror("sem_init");
        munmap(shm, sizeof(*shm));
        shm_unlink(name);
        return EXIT_FAILURE;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        sem_destroy(&shm->ready);
        munmap(shm, sizeof(*shm));
        shm_unlink(name);
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        shm->value = 42;
        snprintf(shm->message, sizeof(shm->message),
                 "written by child %ld", (long)getpid());
        sem_post(&shm->ready);
        _exit(0);
    }

    /* 等待子进程完成写入后再读取共享内存 */
    if (sem_wait(&shm->ready) < 0) {
        perror("sem_wait");
        failures++;
    }

    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        failures++;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        failures++;
    }

    snprintf(expected, sizeof(expected), "written by child %ld", (long)pid);
    printf("shm: value=%d message=\"%s\"\n", shm->value, shm->message);
    if (shm->value != 42 || strcmp(shm->message, expected) != 0)
        failures++;

    sem_destroy(&shm->ready);
    munmap(shm, sizeof(*shm));
    shm_unlink(name);

    if (failures != 0) {
        fprintf(stderr, "shm-sem: FAIL\n");
        return EXIT_FAILURE;
    }

    printf("shm-sem: PASS\n");
    return EXIT_SUCCESS;
}
