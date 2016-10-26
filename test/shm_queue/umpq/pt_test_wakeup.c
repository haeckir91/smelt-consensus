#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>

#include "ump_os.h"
#include "lxaffnuma.h"

struct thr_arg {
	pthread_barrier_t *tbar;
	void              *ump_sleep;
	void              *ump_wakeup;
	int               tid;
};

void *
thread_fn(void *arg)
{
	struct thr_arg *targ = (struct thr_arg *)arg;
	pthread_barrier_wait(targ->tbar);

	ump_sleep(targ->ump_sleep);
	printf("[%d] woken up\n", targ->tid);
	ump_wake_peer(targ->ump_wakeup);

	return NULL;
}

int main(int argc, const char *argv[])
{
	unsigned int ncpus;

	ncpus = aff_get_ncpus();
	pthread_t tids[ncpus];
	struct thr_arg targs[ncpus];
	pthread_barrier_t tbar;

	void *ump_wake_ctx[ncpus];
	for (unsigned int i=0; i<ncpus; i++) {
		ump_wake_context_setup(ump_wake_ctx + i);
	}

	pthread_barrier_init(&tbar, NULL, ncpus+1);

	for (unsigned int i=0; i<ncpus; i++) {
		unsigned int next = (i+1) % ncpus;
		targs[i].tid  = i;
		targs[i].tbar = &tbar;
		targs[i].ump_sleep = ump_wake_ctx[i];
		targs[i].ump_wakeup = ump_wake_ctx[next];
	}

	for (unsigned int i=0; i<ncpus; i++) {
		pthread_create(tids + i, NULL, thread_fn, targs + i);
	}

	pthread_barrier_wait(&tbar);
	ump_wake_peer(ump_wake_ctx[0]); // wake up the first

	for (unsigned int i=0; i<ncpus; i++) {
		pthread_join(tids[i], NULL);
	}

	return 0;
}

// vim:noexpandtab:tabstop=8:softtabstop=0:shiftwidth=8:copyindent
