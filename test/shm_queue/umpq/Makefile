.PHONY: all

CC        = gcc
CFLAGS    = -Wall -std=c99 -O0 -ggdb3
CFLAGS   += -Wpointer-arith -Wreturn-type -Wshadow -Wcast-align
CFLAGS   += -D_GNU_SOURCE -pthread
#CFLAGS   += -DNDEBUG

LIBS     += -L../numactl-2.0.8 -lnuma
CFLAGS   += -DLIBNUMA_

hdrs      = $(wildcard *.h)

ump_deps   = ump_chan.o ump_queue.o ump_txchan.o ump_rxchan.o
bench_deps = lxaffnuma.o parse_int.o

all: ptmain pt_test_wakeup pt_bench_ump pt_bench_ptq pt_bench_ffq pt_bench_pairs_ump pt_bench_pairs_ffq pt_bench_send_batch tree

ptmain: ptmain.o test_driver.o $(ump_deps)
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

pt_test_wakeup: pt_test_wakeup.o lxaffnuma.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

pt_bench_ump: pt_bench_ump.o $(ump_deps) $(bench_deps) ump_conf.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

pt_bench_ptq: pt_bench_ptq.o pt_queue.o $(bench_deps)
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

pt_bench_ffq: pt_bench_ffq.o ff_queue.o $(bench_deps) ffq_conf.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

pt_bench_pairs_ump: pt_bench_pairs_ump.o $(ump_deps) $(bench_deps) ump_conf.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

pt_bench_send_batch: pt_bench_send_batch.o $(ump_deps) $(bench_deps) ump_conf.o barrelfish_compat.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

pt_bench_pairs_ffq: pt_bench_pairs_ffq.o ff_queue.o $(bench_deps) ffq_conf.o
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

tree: main.o model.o barrelfish_compat.o qrm_mgmt.o exp_ab.o exp_ab_thread.o tree_setup.o ump_conf.o $(bench_deps) $(ump_deps) model.h model_defs.h
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

reduction: main.o model.o barrelfish_compat.o qrm_mgmt.o exp_ab.o exp_reduction.c exp_barrier.c exp_reduction_thread.o tree_setup.o ump_conf.o $(bench_deps) $(ump_deps) model.h model_defs.h
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

2pc: main.o model.o barrelfish_compat.o qrm_mgmt.o exp_ab.o exp_2pc.c exp_2pc_thread.o tree_setup.o ump_conf.o $(bench_deps) $(ump_deps) model.h model_defs.h
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

barrier: main.o model.o barrelfish_compat.o qrm_mgmt.o exp_ab.o exp_barrier.c exp_barrier_thread.o tree_setup.o exp_reduction.o ump_conf.o $(bench_deps) $(ump_deps) model.h model_defs.h
	$(CC) $(CFLAGS) $(LIBS) $^ -o $@

parse_int_test: parse_int.c
	$(CC) $(CFLAGS) -DPARSE_INT_TEST $(LIBS) $^ -o $@

%.o: %.c $(hdrs)
	$(CC) $(CFLAGS) $< -c -o $@

%.s: %.c $(hdrs)
	$(CC) $(CFLAGS)-S -fverbose-asm $<

%.i: %.c $(hdrs)
	$(CC)  $(CFLAGS) -E $< | indent -kr > $@

pt_bench_ump_lto: pt_bench_ump.c ump_chan.c ump_queue.c ump_txchan.c ump_rxchan.c lxaffnuma.c parse_int.c ump_conf.c
	$(CC) -flto $(CFLAGS) $(LIBS) $^ -o $@

clean:
	rm -rf *.o ptmain pt_test_ump pt_test_wakeup pt_bench_ump pt_bench_ptq pt_bench_ump_lto pt_bench_pairs_ump pt_bench_pairs_ffq tree pt_bench_send_batch reduction
