#include "barrelfish_compat.h"

#include <sched.h>
#include <unistd.h>

inline coreid_t disp_get_core_id(void) 
{
    return sched_getcpu();
}

coreid_t get_num_cores(void) {
    return sysconf( _SC_NPROCESSORS_ONLN );
}
