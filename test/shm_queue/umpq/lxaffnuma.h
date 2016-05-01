#ifndef LXAFFNUMA_H
#define LXAFFNUMA_H

#define LIBNUMA_ 1

int  aff_get_ncpus(void);

int   numa_cpu_to_node(int cpu);

#endif
