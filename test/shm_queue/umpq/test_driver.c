#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "stdbool.h"

#if defined(__unix__)
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include "ump_queue.h"
#include "test_driver.h"

#ifdef _MSC_VER
#define PRIuPTR     "Iu"
#else
#include <inttypes.h>
#endif

#define LIMIT 100000000

void test_source_thread(struct ump_queue *q)
{
    uintptr_t val = 0;

    printf("source starting\n");
    while (val < LIMIT) {
        ump_enqueue_word(q, val++);
        if (val % 2000000 == 0) {
            printf("source: pause\n");
#ifdef _WIN32
            Sleep(val / 100000);
#elif defined(__unix__)
            usleep(val/100);
#endif
            printf("source: resume\n");
        }
    }
}

void test_sink_thread(struct ump_queue *q)
{
    uintptr_t val, next = 0;

    printf("sink starting\n");
    while (next < LIMIT) {
        ump_dequeue_word(q, &val);
        assert(val == next++);
        if (val % 100000 == 0) {
            printf("sink: %"PRIuPTR"\n", val);
        }
        if (val % 4000000 == 0) {
            printf("sink: pause\n");
#ifdef _WIN32
            Sleep(val / 100000);
#elif defined(__unix__)
            usleep(val/100);
#endif
            printf("sink: resume\n");
        }
    }
}
