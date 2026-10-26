#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_THREADS 4
#define COUNT 1000000

static pthread_mutex_t total_lock = PTHREAD_MUTEX_INITIALIZER;
static long long total;

struct worker {
    int id;
    long long start;
    long long end;
};

static void *sum_range(void *arg)
{
    struct worker *w = arg;
    long long partial = 0;
    long long i;

    for (i = w->start; i < w->end; i++)
        partial += i + 1;

    /* 多个线程更新同一个变量时必须加锁 */
    pthread_mutex_lock(&total_lock);
    total += partial;
    pthread_mutex_unlock(&total_lock);

    printf("thread %d: [%lld, %lld) partial=%lld tid=%ld\n",
           w->id, w->start, w->end, partial, (long)pthread_self());
    return NULL;
}

int main(void)
{
    pthread_t threads[NUM_THREADS];
    struct worker workers[NUM_THREADS];
    long long per = COUNT / NUM_THREADS;
    long long expected = (long long)COUNT * (COUNT + 1) / 2;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < NUM_THREADS; i++) {
        int rc;

        workers[i].id = i;
        workers[i].start = i * per;
        workers[i].end = (i == NUM_THREADS - 1) ? COUNT : (i + 1) * per;

        rc = pthread_create(&threads[i], NULL, sum_range, &workers[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            return EXIT_FAILURE;
        }
    }

    for (i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    printf("pthread sum=%lld expected=%lld\n", total, expected);
    if (total != expected) {
        fprintf(stderr, "pthread: FAIL\n");
        return EXIT_FAILURE;
    }

    printf("pthread: PASS\n");
    return EXIT_SUCCESS;
}
