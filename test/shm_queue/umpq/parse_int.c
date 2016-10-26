/*************************************************************************
 *
 * (general) parsing helpers
 *  (used for option parsing)
 *
 *********************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "parse_int.h"
#include "misc.h"

long
parse_int(const char *s)
{
    long ret;
    char *endptr;

    ret = strtol(s, &endptr, 10);
    if (*endptr != '\0') {
        fprintf(stderr, "parse error: '%s' is not a number\n", s);
        exit(1);
    }

    return ret;
}

// @sep is replaced by '\0', and the index of the next character is returned in
// @idxs
// returns -1 if failed
int
tokenize_by_sep(char *str, char sep, int *idxs, unsigned idxs_size)
{
    unsigned idxs_i, str_i;

    str_i = 0;
    for (idxs_i = 0; idxs_i < idxs_size; idxs_i++) {
        for (;;) {
            char c = str[str_i++];
            if (c == sep) {
                str[str_i-1] = '\0';
                idxs[idxs_i] = str_i;
                break;
            } else if (c == '\0') {
                return -1;
            }
        }
    }

    return 0;
}

/*
 * parse a tuple of integers seperated by commas -- e.g., 1,2,3.
 * put the result in @tuple array.
 *  NULL -> tuple remains unchanged
 *  ,,1  -> only tuple[2] is changed
 */
void
parse_int_tuple(const char *s, int *tuple, unsigned tuple_size)
{
    if (s == NULL || tuple_size == 0)
        return;

    // do a copy
    size_t s_size = strlen(s) + 1;
    char str[s_size];
    memcpy(str, s, s_size);

    const char sep = ',';
    unsigned t_i=0, s_i=0;
    for(;;) {
        unsigned start = s_i;
        for (;;) {
            char c = str[s_i++];
            if (c == '\0' || c == sep)
                break;
        }
        unsigned end = s_i - 1;

        bool done = (str[end] == '\0');

        if (start != end) {
            str[end] = '\0';
            tuple[t_i] = parse_int(str + start);
        }

        if (done || ++t_i >= tuple_size)
            break;
    }
}

// 1,2-10,14 -> [1,2,3,4,5,6,7,8,9,10,14]
// return a tuple, user has to free() it
int *
parse_ints_range(const char *s, unsigned *tuple_size)
{

    if (s == NULL) {
        *tuple_size = 0;
        return NULL;
    }

    // do a copy
    size_t s_size = strlen(s) + 1;
    char str[s_size];
    memcpy(str, s, s_size);

    int *ret = NULL;
    unsigned ret_idx = 0, ret_allocated = 0;
    const unsigned alloc_grain = 32;

    const char sep = ',';
    unsigned s_i=0; // string index
    for(;;) {
        unsigned start = s_i;
        for (;;) {
            char c = str[s_i++];
            if (c == '\0' || c == sep)
                break;
        }
        unsigned end = s_i - 1;
        bool done = (str[end] == '\0');

        if (start != end) {
            int idx = -1;
            char *tmp = str + start;

            str[end] = '\0';
            unsigned off = (tmp[0] == '-') ? 1:0; // for negative numbers
            if (tokenize_by_sep(tmp + off, '-', &idx, 1) == -1) {
                // this is a single int
                {
                    int i = parse_int(tmp);
                    if (ret_idx >= ret_allocated) {
                        if (ret_idx != ret_allocated)
                            printf("ret_idx=%u ret_allocated=%u\n", ret_idx, ret_allocated);
                        assert(ret_idx == ret_allocated);
                        unsigned new_size = (ret_allocated + alloc_grain)*sizeof(int);
                        if (ret == NULL) {
                            assert(ret_allocated == 0);
                            ret = (int*) xmalloc(new_size);
                        } else {
                            ret = (int*) xrealloc(ret, new_size);
                        }

                        ret_allocated += alloc_grain;
                    }

                    assert(ret_allocated > ret_idx);
                    ret[ret_idx++] = i;
                }

            } else {
                // this is a range
                assert(idx > 0);
                int r_start = parse_int(tmp);
                int r_end = parse_int(tmp + off + idx);
                for (int x=r_start; x<=r_end; x++)
                    {
                        int i = x;
                        if (ret_idx >= ret_allocated) {
                            if (ret_idx != ret_allocated)
                                printf("ret_idx=%u ret_allocated=%u\n", ret_idx, ret_allocated);
                            assert(ret_idx == ret_allocated);
                            unsigned new_size = (ret_allocated + alloc_grain)*sizeof(int);
                            if (ret == NULL) {
                                assert(ret_allocated == 0);
                                ret = (int*) xmalloc(new_size);
                            } else {
                                ret = (int*) xrealloc(ret, new_size);
                            }

                            ret_allocated += alloc_grain;
                        }

                        assert(ret_allocated > ret_idx);
                        ret[ret_idx++] = i;
                        
                    }
            }
        }

        if (done)
            break;
    }

    *tuple_size = ret_idx;
    return ret;
}

#if defined(PARSE_INT_TEST)
static void __attribute__((unused))
parse_int_tuple_test(void)
{
    static struct {
        const char *str;
        const int  tuple[16];
        unsigned   tuple_size;
    } test_data[] = {
        {"1,2,3", { 1, 2, 3}, 3},
        {"1,,3",  { 1,-1, 3}, 3},
        {",,," ,  {-1,-1,-1}, 3},
        {""    ,  {-1,-1,-1}, 3},
        {",2," ,  {-1, 2,-1}, 3},
        { NULL  , {0       }, 0}
    };

    for (unsigned ti=0; ;ti++) {
        const char *str     = test_data[ti].str;
        const int  *tuple   = test_data[ti].tuple;
        unsigned tuple_size = test_data[ti].tuple_size;

        if (str == NULL)
            break;

        int test_tuple[tuple_size];
        for (unsigned i=0; i<tuple_size; i++)
            test_tuple[i] = -1;

        parse_int_tuple(str, test_tuple, tuple_size);
        for (unsigned i=0; i<tuple_size; i++)
            if (test_tuple[i] != tuple[i]) {
                fprintf(stderr, "FAIL on '%s' t[%d] %d vs %d\n",
                        str, i, test_tuple[i], tuple[i]);
                abort();
            }
    }
    printf("%s: DONE\n", __FUNCTION__);
}

static void __attribute__((unused))
parse_ints_range_test(void)
{
    static struct {
        const char *str;
        const int  tuple[16];
        unsigned   tuple_size;
    } test_data[] = {
        {"1,2,3",        {1, 2, 3}, 3},
        {"1-5",          {1, 2, 3, 4, 5}, 5},
        {"1,2,4-7,10",   {1, 2, 4, 5, 6, 7, 10}, 7},
        {"-10,-5",       {-10,-5}, 2},
        {"-1-1",         {-1,0,1}, 3},
        {"-3--1",        {-3,-2,-1}, 3},
        { NULL,          {0}, 0}
    };

    for (unsigned ti=0; ;ti++) {
        const char *str     = test_data[ti].str;
        const int  *tuple   = test_data[ti].tuple;
        unsigned tuple_size = test_data[ti].tuple_size;

        if (str == NULL)
            break;

        int *test_tuple;
        unsigned test_tuple_size;

        test_tuple = parse_ints_range(str, &test_tuple_size);
        if (test_tuple_size != tuple_size) {
            fprintf(stderr, "FAIL on '%s' different size:%u vs %u\n",
                    str, test_tuple_size, tuple_size);
            abort();
        }
        for (unsigned i=0; i<tuple_size; i++)
            if (test_tuple[i] != tuple[i]) {
                fprintf(stderr, "FAIL on '%s' t[%d] %d vs %d\n",
                        str, i, test_tuple[i], tuple[i]);
                abort();
            }
        free(test_tuple);
    }
    printf("%s: DONE\n", __FUNCTION__);
}

int main(int argc, const char *argv[])
{
    parse_int_tuple_test();
    parse_ints_range_test();
    return 0;
}
#endif
