/*************************************************************************
 *
 * linux-specific helpers for affinity/NUMA allocation
 *
 ********************************************************/

#include <sched.h>
#include <unistd.h>
#include <stdio.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>

#include "lxaffnuma.h"
#include <assert.h>
#include <stdlib.h>

#if defined(LIBNUMA_) // defined in lxaffnuma.h
#ifdef BARRELFISH
#include <barrelfish/barrelfish.h>
#endif
#include <numa.h>
#endif

// get available CPUs, based on current affinity
int
aff_get_ncpus(void)
{
#if 0
    cpu_set_t mask;
    int err = sched_getaffinity(0, sizeof(mask), &mask);
    if (err < 0) {
        perror("sched_getaffinity");
        abort();
    }
#endif
    assert (!"NYI");

    //    return CPU_COUNT(&mask);
    return 0;
}

/* void * */
/* numa_do_alloc(size_t size, int node) */
/* { */
/*     void *ptr; */
/*     #if defined(LIBNUMA_) */
/*     ptr = numa_alloc_onnode(size, node); */
/*     #else */
/*     ptr = NULL; */
/*     assert (posix_memalign(&ptr, size, getpagesize())==0); */
/*     #endif */
/*     if (!ptr) { */
/*         perror("numa_do_alloc"); */
/*         abort(); */
/*     } */
/*     return ptr; */
/* } */
