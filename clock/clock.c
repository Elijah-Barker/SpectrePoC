#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#include <stdint.h>

// #include <x86intrin.h>

// #ifndef __EMSCRIPTEN_PTHREADS__
// assert("No pthreads!");
// #endif


#define CACHE_FLUSH_ITERATIONS 65536
#define CACHE_FLUSH_STRIDE 64
uint8_t cache_flush_array[CACHE_FLUSH_STRIDE * CACHE_FLUSH_ITERATIONS];

unsigned int junk = 0;
int junk1 = 45;
int junk2 = 0;

int l;
// (void)junk2;

pthread_t loopc;
pthread_t get_c;

int counter = 0;
pthread_mutex_t lock;
int go = 1;
int go2 = 1;
int unused4[64*64] = {45};
int check = 3;
int check2[3] = {3};
int unused5[64*64] = {45};

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
    register uint64_t time1, time2;
    register int y;
    int i, j, max, k, max2, x2;
    int x = j = max = max2 = x2 = 0;

    for (int z = 0; z < 100000; z++){}

    max=0;
    for (j = 0; j < 10; j++)
    {
        x=x2=0;
        for (i = 0; i < 100; i++)
        {
            for (k = 0; k < 10; k++)
            for(l = CACHE_FLUSH_ITERATIONS * CACHE_FLUSH_STRIDE - 1; l >= 0; l-= CACHE_FLUSH_STRIDE) {
                junk2 ^= cache_flush_array[l];
            }
            
            // recreate pthread here
#ifdef INCACHE
            junk2 += check;
#endif
            for (volatile int z = 0; z < 1000; z++)
            {
                reset_counter();
            for (volatile int z = 0; z < 100; z++) {}
                // time1 = __rdtscp( & junk); /* READ TIMER */
                //   junk = * addr; /* MEMORY ACCESS TO TIME */
                junk2 = junk1;
                // time2 = __rdtscp( & junk) - time1; /* READ TIMER & COMPUTE ELAPSED TIME */
            for (volatile int z = 0; z < 100; z++) {}
                y = get_counter();
            }
            reset_counter();
            for (volatile int z = 0; z < 100; z++) {}
            // time1 = __rdtscp( & junk); /* READ TIMER */
            junk2 = check;
            // time2 = __rdtscp( & junk); /* READ TIMER & COMPUTE ELAPSED TIME */
            for (volatile int z = 0; z < 100; z++) {}
            y = get_counter();
            time2 -= time1;
            x += y;
            x2 += time2;
            // printf("pthread: %4d, __rdtscp: %4llu\n", y, time2);
        }
        printf("Cumulative counter was measured: pthread: %4d, __rdtscp: %4d\n", x, x2);
        if (x>max)
        {
            max=x;
        }
        if (x2>max2)
        {
            max2=x2;
        }
    }
    printf("max: %d\n", max);

    go = 0;
    return NULL;
}

int main(void)
{
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
