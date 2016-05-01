#include <stddef.h>

#include "barrelfish_compat.h"
#include "quorum.h"
#include "lxaffnuma.h"

// Thread local variables
__thread coreid_t this_core;

void* thr(void *arg) 
{
    struct thr_arg *targ = arg;
    aff_set_oncpu(targ->core_id);

    this_core = disp_get_core_id();

    tree_init();

    exp_reduction();

    return 0;
}
