all: objs clean bench_queue

C:=g++
MAKEDEPEND:=makedepend -Y

CFLAGS    = -g -Wall -std=c++11 -Wno-literal-suffix -Wno-pointer-arith
CFLAGS   += -DLIBNUMA_ -lnuma
CFLAGS   += -D_GNU_SOURCE -pthread

INC_DIR= -I umpq/

UMPQ=umpq

%.o:: %.c
	$(C) $(CFLAGS) $(LIB) $(INC_DIR) -lnuma -o $@ -c $< 

UMPQ%.o:: UMPQ%.c
	$(C) $(CFLAGS) $(LIB) $(INC_DIR) -o $@ -c $< 


c_FILES=\
main.c\
shm_queue4.c\
incremental_stats.c\

UMP_OBJS += $(UMPQ)/ump_chan.c \
	$(UMPQ)/ump_conf.c \
	$(UMPQ)/ump_queue.c \
	$(UMPQ)/ump_rxchan.c \
	$(UMPQ)/ump_txchan.c \
	$(UMPQ)/lxaffnuma.c \
	$(UMPQ)/parse_int.c

OBJS += $(patsubst %.c,%.o, $(c_FILES))
OBJS += $(patsubst %.c,%.o, $(UMP_OBJS))

objs: $(OBJS)

clean:
	-rm -f *.o
	-rm -f ../*.o
	-rm -f *~; rm -f ; rm -f bench_queue

clobber:
	-rm -f *.o
	-rm -f *~

bench_queue: ; $(C) $(CFLAGS) $(LIB) $(INC_DIR) $(c_FILES) $(UMP_OBJS) -o bench_queue -lnuma

depend:
	$(MAKEDEPEND) $(INCS) $(SINCS) $(C_FILES)

# DO NOT DELETE THIS LINE -- make depend depends on it.
