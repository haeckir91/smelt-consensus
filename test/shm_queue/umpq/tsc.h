#ifndef __TSC__
#define __TSC__

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <inttypes.h>

#define TSC_MINMAX
#define TSC_INDIVIDUAL

struct tsc {
	uint64_t ticks;
	uint64_t last;
	uint64_t cnt;
	#if defined(TSC_MINMAX)
	uint64_t min;
	uint64_t max;
	#endif
};
typedef struct tsc tsc_t;

#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t get_ticks(void)
{
	uint32_t hi,low;
	uint64_t ret;

	__asm__ __volatile__ ("rdtsc" : "=a"(low), "=d"(hi));

	ret = hi;
	ret <<= 32;
	ret |= low;

	return ret;
}
#elif defined(__ia64__)
#include <asm/intrinsics.h>
static inline uint64_t get_ticks(void)
{
	uint64_t ret = ia64_getreg(_IA64_REG_AR_ITC);
	ia64_barrier();

	return ret;
}
#elif defined(__sparc__)
// linux-2.6.28/arch/sparc64/kernel/time.c
static inline uint64_t get_ticks(void)
{
	uint64_t t;
	__asm__ __volatile__ (
		"rd     %%tick, %0\n\t"
		"mov    %0,     %0"
		: "=r"(t)
	);
	return t & (~(1UL << 63));
}
#else
#error "dont know how to count ticks"
#endif

static inline void tsc_init(tsc_t *tsc)
{
	tsc->ticks = 0;
	tsc->last  = 0;
	tsc->cnt   = 0;
	#if defined(TSC_MINMAX)
	tsc->min   = UINT64_MAX;
	tsc->max   = 0;
	#endif
}

static inline void tsc_shut(tsc_t *tsc)
{
}

static inline void tsc_start(tsc_t *tsc)
{
	tsc->last = get_ticks();
	assert(tsc->cnt % 2 == 0);
	tsc->cnt++;
}

static inline uint64_t tsc_pause(tsc_t *tsc)
{
	uint64_t ticks;
	uint64_t t = get_ticks();
	assert(tsc->last < t);
	assert(tsc->cnt % 2 == 1);
	ticks = t - tsc->last;
	tsc->ticks += ticks;
	tsc->cnt++;
	#if defined(TSC_MINMAX)
	if (ticks > tsc->max)
		tsc->max = ticks;
	// XXX: there is code that initializes tsc_t by zeroing everything
	// (i.e., without calling tsc_init())
	if (ticks < tsc->min || tsc->min == 0) {
		tsc->min = ticks;
	}
	#endif

    return ticks;
}

// add tsc2 to tsc1
static inline void tsc_add(tsc_t *tsc1, tsc_t *tsc2)
{
    tsc1->ticks += tsc2->ticks;
    tsc1->cnt += tsc2->cnt;
    #if defined(TSC_MINMAX)
    if (tsc2->max > tsc1->max)
        tsc1->max = tsc2->max;
    if (tsc2->min < tsc1->min)
        tsc1->min = tsc2->min;
    #endif
}

static double __getMhz(void)
{
	double mhz = 0;
#ifdef CPU_MHZ_SH
	FILE *script;
	char buff[512], *endptr;
	int ret;

	script = popen(CPU_MHZ_SH, "r");
	assert(script != NULL);
	ret = fread(buff, 1, sizeof(buff), script);
	if (!ret){
		perror("fread");
		exit(1);
	}

	mhz = strtod(buff, &endptr);
	if (endptr == buff){
		perror("strtod");
		exit(1);
	}
#endif
	return mhz;
}

static inline double getMhz(void)
{
	static double mhz = 0.0;
	if (mhz == 0.0){
		mhz = __getMhz();
	}
	return mhz;
}

static inline double __tsc_getsecs(uint64_t ticks)
{
	return (ticks/(1000000.0*getMhz()));
}
static inline double tsc_getsecs(tsc_t *tsc)
{
	return __tsc_getsecs(tsc->ticks);
}

static uint64_t tsc_getticks(tsc_t *tsc)
{
	return tsc->ticks;
}

static inline uint64_t
tsc_cnt(tsc_t *tsc)
{
	assert(tsc->cnt % 2 == 0 && "counter still running");
	return tsc->cnt / 2;
}

static inline uint64_t
tsc_avg_uint64(tsc_t *tsc)
{
	assert(tsc->cnt % 2 == 0);
	uint64_t cnt = tsc_cnt(tsc);
	return cnt == 0 ? 0: tsc_getticks(tsc) / cnt;
}

static inline double
tsc_avg(tsc_t *tsc)
{
	assert(tsc->cnt % 2 == 0);
	uint64_t cnt = tsc_cnt(tsc);
	return cnt == 0 ? 0.0 : (double)tsc_getticks(tsc) / (double)cnt;
}

static inline uint64_t
tsc_max(tsc_t *tsc)
{
	#if defined(TSC_MINMAX)
	return tsc->max;
	#else
	return tsc_avg_uint64(tsc);
	#endif
}

static inline uint64_t
tsc_min(tsc_t *tsc)
{
	#if defined(TSC_MINMAX)
	return tsc->min;
	#else
	return tsc_avg_uint64(tsc);
	#endif
}

static inline void tsc_spinticks(uint64_t ticks)
{
	uint64_t t0;
	//uint64_t spins = 0;
	t0 = get_ticks();
	for (;;){
		if (get_ticks() - t0 >= ticks)
			break;
		//spins++;
	}
	//printf("spins=%lu\n", spins);
}

static inline char *
tsc_u64_hstr(uint64_t ul)
{
	#define UL_HSTR_NR 16
	static __thread int i=0;
	static __thread char buffs[UL_HSTR_NR][16];
	char *b = buffs[i++ % UL_HSTR_NR];
	#undef UL_HSTR_NR

	char m;
	double t;
	if (ul < 1000) {
		m = ' ';
		t = (double)ul;
	} else if (ul < 1000*1000) {
		m = 'K';
		t = (double)ul/(1000.0);
	} else if (ul < 1000*1000*1000) {
		m = 'M';
		t = (double)ul/(1000.0*1000.0);
	} else {
		m = 'G';
		t = (double)ul/(1000.0*1000.0*1000.0);
	}

	snprintf(b, 16, "%5.1lf%c", t, m);
	return b;

}

static inline void tsc_report_ticks(char *prefix, uint64_t ticks)
{
	printf("%26s: ticks:%7s [%13lu]\n", prefix, tsc_u64_hstr(ticks), ticks);
}

static inline void
tsc_report(const char *prefix, tsc_t *tsc)
{
	uint64_t ticks = tsc_getticks(tsc);
	printf("%26s: ticks:%7s [%13" PRIu64 "]"
	              " cnt:%7s [%13" PRIu64 "]"
	              " avg:%7s [%16.2lf]"
	              #if defined(TSC_MINMAX)
	              " max:%7s [%13" PRIu64 "]"
	              " min:%7s [%13" PRIu64 "]"
	              #endif
	              "\n",
	         prefix,
	         tsc_u64_hstr(ticks),                  ticks,
	         tsc_u64_hstr(tsc_cnt(tsc)),           tsc_cnt(tsc),
	         tsc_u64_hstr((uint64_t)tsc_avg(tsc)), tsc_avg(tsc)
	         #if defined(TSC_MINMAX)
	         ,
	         tsc_u64_hstr(tsc_max(tsc)),           tsc_max(tsc),
	         tsc_u64_hstr(tsc_min(tsc)),           tsc_min(tsc)
	         #endif
	         );
}

#define TSC_REPFL_ZEROES 0x1

// report and percentages based on total ticks
static inline void
tsc_report_perc(const char *prefix, tsc_t *tsc, uint64_t total_ticks,
                unsigned long flags)
{
	if (!(flags & TSC_REPFL_ZEROES) && (tsc_cnt(tsc) == 0) )
		return;

	uint64_t ticks = tsc_getticks(tsc);
	printf("%26s: ticks:%7s [%13" PRIu64 "]"
	              " (%5.1lf%%)"
	              " cnt:%7s [%13" PRIu64 "]"
	              " avg:%7s [%16.2lf]"
	              #if defined(TSC_MINMAX)
	              " max:%7s [%13" PRIu64 "]"
	              " min:%7s [%13" PRIu64 "]"
	              #endif
	              "\n",
	         prefix,
	         tsc_u64_hstr(ticks),                  ticks,
	         ((double)ticks*100.0)/(double)total_ticks,
	         tsc_u64_hstr(tsc_cnt(tsc)),           tsc_cnt(tsc),
	         tsc_u64_hstr((uint64_t)tsc_avg(tsc)), tsc_avg(tsc)
	         #if defined(TSC_MINMAX)
	         ,
	         tsc_u64_hstr(tsc_max(tsc)),           tsc_max(tsc),
	         tsc_u64_hstr(tsc_min(tsc)),           tsc_min(tsc)
	         #endif
	         );
}

static inline void tsc_report_old(char *prefix, tsc_t *tsc)
{
	uint64_t ticks = tsc_getticks(tsc);
	tsc_report_ticks(prefix, ticks);
}

#define TSC_UPDATE(tsc, code) \
do { \
	tsc_start(tsc); \
	do { code } while (0); \
	tsc_pause(tsc); \
} while (0);

#define TSC_MEASURE(tsc_, code_)               \
tsc_t tsc_  = ({                               \
	tsc_t xtsc_;                               \
	tsc_init(&xtsc_); tsc_start(&xtsc_);       \
	do { code_ } while (0);                    \
	tsc_pause(&xtsc_);                         \
	xtsc_;                                     \
});

#define TSC_MEASURE_TICKS(_ticks, _code)       \
uint64_t _ticks = ({                           \
        tsc_t xtsc_;                           \
        tsc_init(&xtsc_); tsc_start(&xtsc_);   \
        do { _code } while (0);                \
        tsc_pause(&xtsc_);                     \
        tsc_getticks(&xtsc_);                  \
});

#define TSC_SET_TICKS(_ticks, _code)           \
_ticks = ({                                   \
        tsc_t xtsc_;                           \
        tsc_init(&xtsc_); tsc_start(&xtsc_);   \
        do { _code } while (0);                \
        tsc_pause(&xtsc_);                     \
        tsc_getticks(&xtsc_);                  \
});

#define TSC_ADD_TICKS(_ticks, _code)           \
_ticks += ({                                   \
        tsc_t xtsc_;                           \
        tsc_init(&xtsc_); tsc_start(&xtsc_);   \
        do { _code } while (0);                \
        tsc_pause(&xtsc_);                     \
        tsc_getticks(&xtsc_);                  \
});

#define TSC_REPORT_TICKS(str, code)                 \
do {                                                \
        tsc_t xtsc_;                                \
        tsc_init(&xtsc_); tsc_start(&xtsc_);        \
        do { code } while (0);                      \
        tsc_pause(&xtsc_);                          \
        tsc_report(str, &xtsc_);                    \
} while (0)


#endif
