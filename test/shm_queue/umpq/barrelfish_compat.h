/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, 2013 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */


#ifndef BARRELFISH_COMPAT_H
#define BARRELFISH_COMPAT_H

#include <inttypes.h>

#define printff(x...) printf(x)
#define PRINTFFF(x...) printf(x)

#ifndef BARRELFISH
typedef uint32_t coreid_t;
typedef uint32_t errval_t;
#endif

coreid_t disp_get_core_id(void);
coreid_t get_num_cores(void);

#endif /* BARRELFISH_COMPAT_H */

