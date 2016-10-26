#ifndef __TEST_DRIVER_H
#define __TEST_DRIVER_H

struct ump_queue;

void test_source_thread(struct ump_queue *q);
void test_sink_thread(struct ump_queue *q);

#endif // __TEST_DRIVER_H
