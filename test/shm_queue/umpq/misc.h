#ifndef MISC_H__
#define MISC_H__

/* various helpers */

#include <stdlib.h> // malloc
#include <stdio.h> // perror

#define xmalloc(s) ({          \
    void *ret_ = malloc(s);    \
    if (ret_ == NULL) {        \
        perror("malloc");      \
        exit(1);               \
    }                          \
    ret_;})

#define xrealloc(ptr, s) ({           \
         void *ret_ = realloc(ptr, s); \
         if (ret_ == NULL) {           \
                 perror("realloc");    \
                 exit(1);              \
         }                             \
         ret_;})


static inline char *
ul_hstr(unsigned long ul)
{
	#define UL_HSTR_NR 16
	static __thread int i=0;
	static __thread char buffs[UL_HSTR_NR][16];
	char *b = buffs[i++ % UL_HSTR_NR];
	#undef UL_HSTR_NR

	const char *m;
	double t;
	if (ul < 1000) {
		m = " ";
		t = (double)ul;
	} else if (ul < 1000*1000) {
		m = "K";
		t = (double)ul/(1000.0);
	} else if (ul < 1000*1000*1000) {
		m = "M";
		t = (double)ul/(1000.0*1000.0);
	} else {
		m = "G";
		t = (double)ul/(1000.0*1000.0*1000.0);
	}

	snprintf(b, 16, "%5.1lf%s", t, m);
	return b;

}

/**
 * Sleep/wakeup seems to be broken for both, the pthread and futex
 * version. I was running into situations where every thread would be
 * sleeping at the same time, with no one left to wake any of them up.
 *
 * I suspect there is a race between going to sleep, in the sense that
 * (perhaps) the last thread goes to sleep thinking that there is
 * another thread left, with the other thread doing the same.
 *
 *   SK - 2015/11/26
 */

//#define UMP_ENABLE_SLEEP 1  // < sleep when {de/en}queue fails repeatedly
//#define UMP_DBG_COUNT  1    // < count important events

#ifdef UMP_ENABLE_SLEEP
#define ump_sleep_enabled() true
#else
#define ump_sleep_enabled() false
#endif

#define CL_SIZE 64 // cache-line size
#if defined(__GNUC__)
#define CACHE_ALIGNED __attribute__ ((aligned(CL_SIZE)))
#else
#define CACHE_ALIGNED
#warning "Don't know how to align struct"
#endif

#endif /* MISC_H__ */
