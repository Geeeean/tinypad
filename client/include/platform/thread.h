#pragma once

// Tiny cross-platform "fire and forget" background thread + sleep, just
// enough for main.c's single polling thread. Not a general threading API.

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef void (*thread_fn_t)(void *arg);

typedef struct {
    thread_fn_t fn;
    void *arg;
} thread_trampoline_args_t;

#ifdef _WIN32
#include <windows.h>

typedef HANDLE thread_t;

static inline DWORD WINAPI thread_trampoline(LPVOID p)
{
    thread_trampoline_args_t *args = (thread_trampoline_args_t *)p;
    args->fn(args->arg);
    free(args);
    return 0;
}

static inline bool thread_start(thread_t *t, thread_fn_t fn, void *arg)
{
    thread_trampoline_args_t *args = malloc(sizeof(*args));
    if (!args) {
        return false;
    }
    args->fn = fn;
    args->arg = arg;
    *t = CreateThread(NULL, 0, thread_trampoline, args, 0, NULL);
    return *t != NULL;
}

static inline void thread_join(thread_t t) { WaitForSingleObject(t, INFINITE); }
static inline void sleep_ms(int ms) { Sleep((DWORD)ms); }
static inline uint64_t monotonic_ms(void) { return (uint64_t)GetTickCount64(); }

#else
#include <pthread.h>
#include <time.h>

typedef pthread_t thread_t;

static inline void *thread_trampoline(void *p)
{
    thread_trampoline_args_t *args = (thread_trampoline_args_t *)p;
    args->fn(args->arg);
    free(args);
    return NULL;
}

static inline bool thread_start(thread_t *t, thread_fn_t fn, void *arg)
{
    thread_trampoline_args_t *args = malloc(sizeof(*args));
    if (!args) {
        return false;
    }
    args->fn = fn;
    args->arg = arg;
    return pthread_create(t, NULL, thread_trampoline, args) == 0;
}

static inline void thread_join(thread_t t) { pthread_join(t, NULL); }

static inline void sleep_ms(int ms)
{
    struct timespec ts = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static inline uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

#endif
