#ifndef PARSE_INT_H__
#define PARSE_INT_H__

long parse_int(const char *s);
void parse_int_tuple(const char *s, int *tuple, unsigned tuple_size);

int tokenize_by_sep(char *str, char sep, int *idxs, unsigned idxs_size);

// 1,2-10,14 -> [1,2,3,4,5,6,7,8,9,10,14]
// return a tuple, user has to free() it
int *
parse_ints_range(const char *s, unsigned *tuple_size);

#endif
