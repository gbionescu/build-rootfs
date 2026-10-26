#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define FILE_SIZE 65536

int main(void)
{
    char path[] = "/tmp/linux-app-mmap-XXXXXX";
    char *mapped;
    char *check;
    ssize_t n;
    int fd;
    int i;
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        return EXIT_FAILURE;
    }

    if (ftruncate(fd, FILE_SIZE) < 0) {
        perror("ftruncate");
        close(fd);
        unlink(path);
        return EXIT_FAILURE;
    }

    mapped = mmap(NULL, FILE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        close(fd);
        unlink(path);
        return EXIT_FAILURE;
    }

    /* 像操作内存一样直接写文件 */
    for (i = 0; i < FILE_SIZE; i++)
        mapped[i] = (char)('A' + i % 26);

    /* MS_SYNC 确保修改立即回写到磁盘文件 */
    if (msync(mapped, FILE_SIZE, MS_SYNC) < 0) {
        perror("msync");
        failures++;
    }

    munmap(mapped, FILE_SIZE);
    close(fd);

    /* 重新打开文件，确认数据已经持久化 */
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        unlink(path);
        return EXIT_FAILURE;
    }

    check = malloc(FILE_SIZE);
    if (check == NULL) {
        perror("malloc");
        close(fd);
        unlink(path);
        return EXIT_FAILURE;
    }

    n = read(fd, check, FILE_SIZE);
    close(fd);
    if (n != FILE_SIZE) {
        fprintf(stderr, "mmap: short read (%zd bytes)\n", n);
        failures++;
    } else {
        for (i = 0; i < FILE_SIZE; i++) {
            if (check[i] != (char)('A' + i % 26)) {
                fprintf(stderr, "mmap: data mismatch at offset %d\n", i);
                failures++;
                break;
            }
        }
        printf("mmap: wrote and verified %d bytes (first=%c last=%c)\n",
               FILE_SIZE, check[0], check[FILE_SIZE - 1]);
    }

    free(check);
    unlink(path);

    if (failures != 0) {
        fprintf(stderr, "mmap: FAIL\n");
        return EXIT_FAILURE;
    }

    printf("mmap: PASS\n");
    return EXIT_SUCCESS;
}
