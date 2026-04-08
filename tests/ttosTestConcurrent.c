#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include "ttosVaddrAlloc.h"
#include "ttosTestFramework.h"

/* -----------------------------------------------------------------------
 * 可移植屏障（macOS 默认不提供 pthread_barrier_t）
 * --------------------------------------------------------------------- */

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    unsigned        count;
    unsigned        waiting;
    unsigned        generation;
} simple_barrier_t;

static void simple_barrier_init(simple_barrier_t *b, unsigned count)
{
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->count      = count;
    b->waiting    = 0;
    b->generation = 0;
}

static void simple_barrier_wait(simple_barrier_t *b)
{
    pthread_mutex_lock(&b->mutex);
    unsigned gen = b->generation;
    b->waiting++;
    if (b->waiting == b->count)
    {
        b->generation++;
        b->waiting = 0;
        pthread_cond_broadcast(&b->cond);
    }
    else
    {
        while (b->generation == gen)
            pthread_cond_wait(&b->cond, &b->mutex);
    }
    pthread_mutex_unlock(&b->mutex);
}

static void simple_barrier_destroy(simple_barrier_t *b)
{
    pthread_mutex_destroy(&b->mutex);
    pthread_cond_destroy(&b->cond);
}

#define ARENA_BASE  0x20000000UL
#define ARENA_PAGES 1024U
#define ARENA_SIZE  (ARENA_PAGES * 4096U)
#define PAGE_SIZE   4096U

/* -----------------------------------------------------------------------
 * 测试一：多线程交替 alloc/free
 *
 * 每个线程循环分配一块虚拟地址后立即释放。
 * 全部线程结束后 arena 必须完全归还。
 * --------------------------------------------------------------------- */

#define T1_THREADS    8
#define T1_ITERATIONS 200

static void *t1_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < T1_ITERATIONS; i++)
    {
        /* 变化分配大小以覆盖小块和大块两条路径 */
        size_t pages = 1 + (size_t)(i % 32);
        void *addr   = TTOS_AllocVaddr(pages * PAGE_SIZE);
        if (addr)
            TTOS_FreeVaddr(addr, pages * PAGE_SIZE);
    }
    return NULL;
}

static void test_concurrent_alloc_free(void)
{
    TEST_SUITE_BEGIN("concurrent alloc-free loop");

    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE) == 0);

    pthread_t threads[T1_THREADS];
    for (int i = 0; i < T1_THREADS; i++)
        pthread_create(&threads[i], NULL, t1_worker, NULL);
    for (int i = 0; i < T1_THREADS; i++)
        pthread_join(threads[i], NULL);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);
    TEST_ASSERT(stats.used_pages == 0);

    TTOS_VaddrArenaDestroy();
}

/* -----------------------------------------------------------------------
 * 测试二：并行 alloc，屏障同步后并行 free
 *
 * 阶段一：所有线程各自分配 N 块并保存地址。
 * 阶段二：屏障同步后所有线程并发释放。
 * 最终 arena 必须完全归还。
 * --------------------------------------------------------------------- */

#define T2_THREADS      4
#define T2_ALLOCS_EACH  8

typedef struct
{
    simple_barrier_t *barrier;
    void             *addrs[T2_ALLOCS_EACH];
    int               alloc_count;
} t2_thread_arg_t;

static void *t2_worker(void *arg)
{
    t2_thread_arg_t *ta = (t2_thread_arg_t *)arg;

    /* 阶段一：分配 */
    ta->alloc_count = 0;
    for (int i = 0; i < T2_ALLOCS_EACH; i++)
    {
        void *a = TTOS_AllocVaddr(PAGE_SIZE);
        if (a)
            ta->addrs[ta->alloc_count++] = a;
    }

    /* 等待所有线程完成分配 */
    simple_barrier_wait(ta->barrier);

    /* 阶段二：释放 */
    for (int i = 0; i < ta->alloc_count; i++)
        TTOS_FreeVaddr(ta->addrs[i], PAGE_SIZE);

    return NULL;
}

static void test_parallel_alloc_then_free(void)
{
    TEST_SUITE_BEGIN("parallel alloc then parallel free");

    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE) == 0);

    simple_barrier_t barrier;
    simple_barrier_init(&barrier, T2_THREADS);

    t2_thread_arg_t args[T2_THREADS];
    pthread_t       threads[T2_THREADS];

    for (int i = 0; i < T2_THREADS; i++)
    {
        args[i].barrier = &barrier;
        pthread_create(&threads[i], NULL, t2_worker, &args[i]);
    }
    for (int i = 0; i < T2_THREADS; i++)
        pthread_join(threads[i], NULL);

    simple_barrier_destroy(&barrier);

    int total_alloced = 0;
    for (int i = 0; i < T2_THREADS; i++)
        total_alloced += args[i].alloc_count;

    TEST_ASSERT(total_alloced <= T2_THREADS * T2_ALLOCS_EACH);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

/* -----------------------------------------------------------------------
 * 测试三：并发分配时地址无重叠
 *
 * 多线程同时分配，收集所有地址范围后两两检查不得有页重叠。
 * --------------------------------------------------------------------- */

#define T3_THREADS  6
#define T3_ALLOCS   16

typedef struct
{
    uintptr_t start;
    size_t    pages;
} addr_range_t;

typedef struct
{
    simple_barrier_t *barrier;
    addr_range_t      ranges[T3_ALLOCS];
    int               count;
} t3_thread_arg_t;

static void *t3_worker(void *arg)
{
    t3_thread_arg_t *ta = (t3_thread_arg_t *)arg;
    ta->count = 0;

    simple_barrier_wait(ta->barrier); /* 同步启动 */

    for (int i = 0; i < T3_ALLOCS; i++)
    {
        void *a = TTOS_AllocVaddr(2 * PAGE_SIZE);
        if (a)
        {
            ta->ranges[ta->count].start = (uintptr_t)a;
            ta->ranges[ta->count].pages = 2;
            ta->count++;
        }
    }
    return NULL;
}

static int ranges_overlap(addr_range_t x, addr_range_t y)
{
    uintptr_t x_end = x.start + x.pages * PAGE_SIZE;
    uintptr_t y_end = y.start + y.pages * PAGE_SIZE;
    return x.start < y_end && y.start < x_end;
}

static void test_no_overlapping_addresses(void)
{
    TEST_SUITE_BEGIN("no overlapping addresses under concurrent load");

    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE) == 0);

    simple_barrier_t barrier;
    simple_barrier_init(&barrier, T3_THREADS);

    t3_thread_arg_t args[T3_THREADS];
    pthread_t       threads[T3_THREADS];

    for (int i = 0; i < T3_THREADS; i++)
    {
        args[i].barrier = &barrier;
        pthread_create(&threads[i], NULL, t3_worker, &args[i]);
    }
    for (int i = 0; i < T3_THREADS; i++)
        pthread_join(threads[i], NULL);

    simple_barrier_destroy(&barrier);

    /* 收集所有地址范围 */
    addr_range_t all[T3_THREADS * T3_ALLOCS];
    int total = 0;
    for (int i = 0; i < T3_THREADS; i++)
        for (int j = 0; j < args[i].count; j++)
            all[total++] = args[i].ranges[j];

    /* 两两检查：不得有页重叠 */
    int overlaps_found = 0;
    for (int i = 0; i < total; i++)
        for (int j = i + 1; j < total; j++)
            if (ranges_overlap(all[i], all[j]))
                overlaps_found++;

    TEST_ASSERT(overlaps_found == 0);

    /* 释放全部 */
    for (int i = 0; i < total; i++)
        TTOS_FreeVaddr((void *)all[i].start, all[i].pages * PAGE_SIZE);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

int main(void)
{
    test_concurrent_alloc_free();
    test_parallel_alloc_then_free();
    test_no_overlapping_addresses();

    TEST_SUITE_RESULTS();
}
