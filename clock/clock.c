#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

// #ifdef _MSC_VER
// #include <intrin.h> /* for rdtsc, rdtscp, clflush */
// #pragma optimize("gt", on)
// #else
// #include <x86intrin.h> /* for rdtsc, rdtscp, clflush */
// #endif                 /* ifdef _MSC_VER */

#ifndef __EMSCRIPTEN_PTHREADS__
assert("No pthreads!");
#endif


#define NTHREADS 150

pthread_t loopc;
pthread_t get_c;

int counter = 0;
pthread_mutex_t lock;
int go = 1;
int myarray[512] = {45};
int check = 3;

void reset_counter()
{
    pthread_mutex_lock(&lock);
    counter = 0;
    pthread_mutex_unlock(&lock);
}

int get_counter()
{
    return counter;
}

void *loop_counter()
{
    while (go)
    {
        pthread_mutex_lock(&lock);
        counter++;
        pthread_mutex_unlock(&lock);
    }
    // printf("Counter has ended: %d\n", counter);
    return NULL;
}

void *try_time()
{
    int z, i, y;
    int x = 0;
    for (z = 0; z < 100000; z++)
        ;

    for (i = 0; i < 1; i++)
    {
        // _mm_clflush( &check );

        reset_counter();
        for (z = 0; z < 80; z++)
            ;

        // int y = check;
        y = get_counter();
        x += y;
    }
    printf("Cumulative counter was measured: %d\n", x);

    go = 0;
    return NULL;
}

int main(void)
{
    // int i = 0;
    int error;

    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        printf("\n mutex init has failed\n");
        return 1;
    }

    error = pthread_create(&(loopc), NULL, &loop_counter, NULL);
    if (error != 0)
        printf("\nThread can't be created :[%s]", strerror(error));

    error = pthread_create(&(get_c), NULL, &try_time, NULL);
    if (error != 0)
        printf("\nThread can't be created :[%s]", strerror(error));

    pthread_join(loopc, NULL);
    pthread_join(get_c, NULL);

    pthread_mutex_destroy(&lock);
    // printf("%d\n", check);

    return 0;
}
