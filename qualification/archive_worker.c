#include <assert.h>
#include <stdlib.h>
#include <windows.h>

#define CELL_OK 0

typedef struct ez_job {
    struct ez_job* next;
} ez_job;

static SRWLOCK s_ezq_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE s_ez_cv = CONDITION_VARIABLE_INIT;
static ez_job* s_ez_first;
static ez_job* s_ez_last;
static volatile LONG s_ez_started;
static unsigned attempts;
static unsigned inline_completions;
static HANDLE completed;

static void ez_process(ez_job* job)
{
    assert(job);
    ++inline_completions;
}

static DWORD WINAPI ez_worker(LPVOID unused)
{
    (void)unused;
    AcquireSRWLockExclusive(&s_ezq_lock);
    while (!s_ez_first)
        assert(SleepConditionVariableSRW(&s_ez_cv, &s_ezq_lock, 5000, 0));
    assert(s_ez_first == s_ez_last && !s_ez_first->next);
    free(s_ez_first);
    s_ez_first = s_ez_last = NULL;
    ReleaseSRWLockExclusive(&s_ezq_lock);
    assert(SetEvent(completed));
    return 0;
}

static HANDLE fail_first_start(LPSECURITY_ATTRIBUTES security, SIZE_T stack_size,
                               LPTHREAD_START_ROUTINE worker, LPVOID argument,
                               DWORD flags, LPDWORD thread_id)
{
    if (++attempts == 1)
        return NULL;
    return CreateThread(security, stack_size, worker, argument, flags, thread_id);
}

static int submit(ez_job* j)
{
#define CreateThread fail_first_start
#include "archive_worker_submit.h"
#undef CreateThread
}

int main(void)
{
    assert(_putenv("RACACIT_LFQ_ASYNC=1") == 0);
    completed = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(completed);
    ez_job* first = calloc(1, sizeof *first);
    ez_job* second = calloc(1, sizeof *second);
    assert(first && second);
    assert(submit(first) == CELL_OK);
    assert(attempts == 1 && inline_completions == 1 && !s_ez_started);
    assert(!s_ez_first && !s_ez_last);
    assert(submit(second) == CELL_OK);
    assert(attempts == 2 && inline_completions == 1 && s_ez_started);
    assert(WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0);
    assert(CloseHandle(completed));
}
