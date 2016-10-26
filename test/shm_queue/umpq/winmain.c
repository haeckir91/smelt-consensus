#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN 1

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "ump_chan.h"
#include "ump_queue.h"
#include "ump_os.h"
#include "test_driver.h"

static const char *g_appname;

// state private to source or sink
struct private_state {
    struct ump_chan chan;
    struct ump_queue queue;
};

static DWORD WINAPI source_thread(void *param)
{
    struct private_state *st = param;
    test_source_thread(&st->queue);
    return 0;
}

static DWORD WINAPI sink_thread(void *param)
{
    struct private_state *st = param;
    test_sink_thread(&st->queue);
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "Usage: %s [option...] source_core sink_core\n", g_appname);
    exit(1);
}

static void die(void)
{
    exit(2);
}

static BYTE parse_core_id_or_die(const char *str)
{
    unsigned long ret;
    char *endptr = NULL;

    ret = strtoul(str, &endptr, 0);
    if (endptr == NULL || *endptr != '\0' || ret > CHAR_MAX) {
        fprintf(stderr, "Failed to parse '%s' as core ID\n", str);
        usage();
    }

    return (BYTE)ret;
}

static HANDLE create_pinned_thread_or_die(LPTHREAD_START_ROUTINE func,
                                          LPVOID param,
                                          PPROCESSOR_NUMBER procnum)
{
    HANDLE h;
    DWORD suspend_count;

    h = CreateThread(NULL, 0, func, param, CREATE_SUSPENDED, NULL);
    if (h == NULL) {
        fprintf(stderr, "Failed to create thread: %x\n", GetLastError());
        die();
    }

    if (!SetThreadIdealProcessorEx(h, procnum, NULL)) {
        fprintf(stderr, "Failed to pin thread: %x\n", GetLastError());
        die();
    }

    suspend_count = ResumeThread(h);
    if (suspend_count == -1) {
        fprintf(stderr, "Failed to resume thread: %x\n", GetLastError());
        die();
    }
    assert(suspend_count == 1);

    return h;
}

int main(int argc, char *argv[])
{
    int i;
    HANDLE source_handle, sink_handle;
    void *tx_event, *rx_event;
    PROCESSOR_NUMBER procnum;
    BYTE source_core, sink_core;
    struct private_state *source_state, *sink_state;
    void *shmem;
    char *endptr = NULL;
    const size_t VM_SIZE = 4096 * 4;
    DWORD ret;

    g_appname = argv[0];

    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            break;
        } else if (strcmp(argv[i], "-h") == 0) {
            usage();
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argv[i]);
            usage();
        }
    }

    if (i + 2 != argc) {
        usage();
    }
    
    source_core = parse_core_id_or_die(argv[i]);
    sink_core = parse_core_id_or_die(argv[i + 1]);

    // TODO: VirtualAllocExNuma / ump_chan_init_numa()
    // http://msdn.microsoft.com/en-us/library/windows/desktop/aa965223(v=vs.85).aspx
    shmem = VirtualAlloc(NULL, VM_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (shmem == NULL) {
        fprintf(stderr, "VirtualAlloc(%x) failed: %x\n", VM_SIZE, GetLastError());
        die();
    }

    source_state = VirtualAlloc(NULL, sizeof(struct private_state),
                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(source_state != NULL);

    sink_state = VirtualAlloc(NULL, sizeof(struct private_state),
                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(sink_state != NULL);

    ump_wake_context_setup(&tx_event);
    ump_wake_context_setup(&rx_event);

    if (!ump_chan_init(&source_state->chan, shmem, VM_SIZE, true, rx_event)) {
        fprintf(stderr, "ump_chan_init failed for source\n");
        die();
    }

    if (!ump_chan_init(&sink_state->chan, shmem, VM_SIZE, false, tx_event)) {
        fprintf(stderr, "ump_chan_init failed for sink\n");
        die();
    }

    ump_queue_init(&source_state->queue, &source_state->chan, tx_event);
    ump_queue_init(&sink_state->queue, &sink_state->chan, rx_event);

    GetCurrentProcessorNumberEx(&procnum);
    printf("Current processor: %u.%u\n", procnum.Group, procnum.Number);
    procnum.Number = source_core;
    source_handle = create_pinned_thread_or_die(source_thread, source_state, &procnum);
    procnum.Number = sink_core;
    sink_handle = create_pinned_thread_or_die(sink_thread, sink_state, &procnum);

    ret = WaitForSingleObject(sink_handle, INFINITE);
    assert(ret == WAIT_OBJECT_0);

    ret = WaitForSingleObject(source_handle, INFINITE);
    assert(ret == WAIT_OBJECT_0);

    CloseHandle(sink_handle);
    CloseHandle(source_handle);

	return 0;
}
