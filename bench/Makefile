all: objs clean start_bench start_bench_libsync

C:=gcc
MAKEDEPEND:=makedepend -Y

INC_DIR = -I../includes/ -I../../smelt-runtime-system/inc/backends/ -I../../smelt-runtime-system/inc/ -I../../umpq/ 

LIB = -L../../smelt-runtime-system/

CFLAGS    = -g -Wall -std=c99
CFLAGS   += -DLIBNUMA_ -lnuma
CFLAGS   += -D_GNU_SOURCE -pthread

SMLT_FLAG = -DSMELT=1

%.o:: %.c
	$(C) $(CFLAGS) $(LIB) $(INC_DIR) -o $@ -c $< 

c_FILES=\
main.c\
../com_layer.c\
../replica.c\
../tpc_replica.c\
../one_replica.c\
../shm_queue.c\
../broadcast_replica.c\
../chain_replica.c\
../client.c\
../incremental_stats.c\
../raft_replica.c\
../kvs_replica.c\
../kvs_client.c\

H_FILES := $(C_FILES:%.C=%.H)

h_FILES := $(c_FILES:%.c=%.h)

OBJ_FILES := $(C_FILES:%.cc=%.o) $(c_FILES:%.c=%.o) $(OBJ_FILES)

objs: $(OBJ_FILES)

clean:
	-rm -f *.o
	-rm -f ../*.o
	-rm -f *~; rm -f start_bench; rm -f start_bench_smelt

clobber:
	-rm -f *.o
	-rm -f *~

start_bench: ; $(C) $(CFLAGS) $(LIB) $(INC_DIR) $(c_FILES) -o start_bench -lsmltrt -lnuma

start_bench_smelt: ; $(C) $(CFLAGS) $(SMLT_FLAG) $(LIB) $(INC_DIR) $(c_FILES) -o start_bench_smelt -lsmltrt -lnuma

depend:
	$(MAKEDEPEND) $(INCS) $(SINCS) $(C_FILES)

# DO NOT DELETE THIS LINE -- make depend depends on it.
