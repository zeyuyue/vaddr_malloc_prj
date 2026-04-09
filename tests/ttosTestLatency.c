/************************************************************************************************
 *              科东(广州)软件科技有限公司 版权所有
 *   Copyright (C) 2021 Intewell (Guangzhou) Software Technology Co., Ltd. All Rights Reserved.
 *************************************************************************************************/

/*
 * 修改历史：
 * 2026-04-08     岳泽宇，科东(广州)软件科技有限公司
 *               创建该文件。
 */

/*
 * @file:  ttosTestLatency.c
 * @brief:
 *       <li>接口响应时间（延迟）测试，验证分配/释放操作的实时性表现。</li>
 */

/*
 * @brief:
 *       <li>对 TTOS_AllocVaddr / TTOS_FreeVaddr 在不同负载场景下进行
 *           单次调用耗时测量，输出最小/最大/平均值供实时性评估参考。</li>
 *       <li>场景一：空闲 arena 连续分配（最佳情况）。</li>
 *       <li>场景二：高碎片化下的分配（最坏情况）。</li>
 *       <li>场景三：释放后合并操作的耗时。</li>
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include "ttosVaddrAlloc.h"
#include "ttosTestFramework.h"

#define PAGE_SIZE   TTOS_PAGE_SIZE
#define ARENA_BASE  (0x10000000UL)
#define ARENA_PAGES (4096U)
#define ARENA_SIZE  (ARENA_PAGES * PAGE_SIZE)

/* 获取当前时间戳（纳秒） */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* -----------------------------------------------------------------------
 * 场景一：空闲 arena 连续分配
 *
 * arena 初始完全空闲，连续分配单页，测量每次 alloc 耗时。
 * 这是最佳情况——空闲段链表只有一个节点，首次适配 O(1)。
 * --------------------------------------------------------------------- */

#define S1_COUNT (512)

static void test_latency_sequential_alloc(void)
{
    void    *addrs[S1_COUNT];
    uint64_t durations[S1_COUNT];
    uint64_t total;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t t0;
    uint64_t t1;
    int      i;

    TEST_SUITE_BEGIN("latency: sequential alloc (best case)");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    total  = 0;
    min_ns = UINT64_MAX;
    max_ns = 0;

    for (i = 0; i < S1_COUNT; i++)
    {
        t0       = now_ns();
        addrs[i] = TTOS_AllocVaddr(PAGE_SIZE, 0);
        t1       = now_ns();

        TEST_ASSERT(addrs[i] != NULL);
        durations[i] = t1 - t0;
        total += durations[i];
        if (durations[i] < min_ns) min_ns = durations[i];
        if (durations[i] > max_ns) max_ns = durations[i];
    }

    printf("  alloc x%d: min=%llu ns, max=%llu ns, avg=%llu ns\n",
           S1_COUNT,
           (unsigned long long)min_ns,
           (unsigned long long)max_ns,
           (unsigned long long)(total / S1_COUNT));

    /* 确保耗时已被测量 */
    TEST_ASSERT(total > 0);

    /* 清理 */
    for (i = 0; i < S1_COUNT; i++)
    {
        TTOS_FreeVaddr(addrs[i], PAGE_SIZE);
    }

    TTOS_VaddrArenaDestroy();
}

/* -----------------------------------------------------------------------
 * 场景二：高碎片化下的分配
 *
 * 先分配所有页，再释放偶数索引页制造最大碎片化（空闲段数 = total/2），
 * 然后测量在碎片化状态下分配单页的耗时。
 * 这是最坏情况——空闲段链表节点数最多，首次适配搜索最长。
 * --------------------------------------------------------------------- */

#define S2_TOTAL  (1024)
#define S2_MEASURE (256)

static void test_latency_fragmented_alloc(void)
{
    void    *addrs[S2_TOTAL];
    void    *measured[S2_MEASURE];
    uint64_t total;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t t0;
    uint64_t t1;
    uint64_t d;
    int      i;

    TEST_SUITE_BEGIN("latency: fragmented alloc (worst case)");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /* 阶段一：逐页分配 */
    for (i = 0; i < S2_TOTAL; i++)
    {
        addrs[i] = TTOS_AllocVaddr(PAGE_SIZE, 0);
        TEST_ASSERT(addrs[i] != NULL);
    }

    /* 阶段二：释放偶数页，制造碎片 */
    for (i = 0; i < S2_TOTAL; i += 2)
    {
        TTOS_FreeVaddr(addrs[i], PAGE_SIZE);
    }

    /* 阶段三：在碎片化状态下分配 */
    total  = 0;
    min_ns = UINT64_MAX;
    max_ns = 0;

    for (i = 0; i < S2_MEASURE; i++)
    {
        t0          = now_ns();
        measured[i] = TTOS_AllocVaddr(PAGE_SIZE, 0);
        t1          = now_ns();

        TEST_ASSERT(measured[i] != NULL);
        d      = t1 - t0;
        total += d;
        if (d < min_ns) min_ns = d;
        if (d > max_ns) max_ns = d;
    }

    printf("  alloc x%d (fragmented): min=%llu ns, max=%llu ns, avg=%llu ns\n",
           S2_MEASURE,
           (unsigned long long)min_ns,
           (unsigned long long)max_ns,
           (unsigned long long)(total / S2_MEASURE));

    TEST_ASSERT(total > 0);

    /* 清理 */
    for (i = 0; i < S2_MEASURE; i++)
    {
        TTOS_FreeVaddr(measured[i], PAGE_SIZE);
    }
    for (i = 1; i < S2_TOTAL; i += 2)
    {
        TTOS_FreeVaddr(addrs[i], PAGE_SIZE);
    }

    TTOS_VaddrArenaDestroy();
}

/* -----------------------------------------------------------------------
 * 场景三：释放并合并
 *
 * 分配三个相邻块，释放两端后测量释放中间块（触发三段合并）的耗时。
 * 重复多轮取统计。
 * --------------------------------------------------------------------- */

#define S3_ROUNDS (200)

static void test_latency_free_coalesce(void)
{
    uint64_t total;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t t0;
    uint64_t t1;
    uint64_t d;
    void    *a;
    void    *b;
    void    *c;
    int      r;

    TEST_SUITE_BEGIN("latency: free with coalesce");

    total  = 0;
    min_ns = UINT64_MAX;
    max_ns = 0;

    for (r = 0; r < S3_ROUNDS; r++)
    {
        TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

        a = TTOS_AllocVaddr(4 * PAGE_SIZE, 0);
        b = TTOS_AllocVaddr(4 * PAGE_SIZE, 0);
        c = TTOS_AllocVaddr(4 * PAGE_SIZE, 0);
        TEST_ASSERT(a && b && c);

        TTOS_FreeVaddr(a, 4 * PAGE_SIZE);
        TTOS_FreeVaddr(c, 4 * PAGE_SIZE);

        /* 测量释放中间块（触发与前后合并） */
        t0 = now_ns();
        TTOS_FreeVaddr(b, 4 * PAGE_SIZE);
        t1 = now_ns();

        d      = t1 - t0;
        total += d;
        if (d < min_ns) min_ns = d;
        if (d > max_ns) max_ns = d;

        TTOS_VaddrArenaDestroy();
    }

    printf("  free+coalesce x%d: min=%llu ns, max=%llu ns, avg=%llu ns\n",
           S3_ROUNDS,
           (unsigned long long)min_ns,
           (unsigned long long)max_ns,
           (unsigned long long)(total / S3_ROUNDS));

    TEST_ASSERT(total > 0);
}

/* -----------------------------------------------------------------------
 * 场景四：对齐分配耗时
 *
 * 测量带对齐约束（16 * PAGE_SIZE）的分配耗时，
 * 与无对齐分配对比，观察对齐搜索的额外开销。
 * --------------------------------------------------------------------- */

#define S4_COUNT (64)

static void test_latency_aligned_alloc(void)
{
    void    *addrs[S4_COUNT];
    uint64_t total;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t t0;
    uint64_t t1;
    uint64_t d;
    int      i;

    TEST_SUITE_BEGIN("latency: aligned alloc (16-page alignment)");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    total  = 0;
    min_ns = UINT64_MAX;
    max_ns = 0;

    for (i = 0; i < S4_COUNT; i++)
    {
        t0       = now_ns();
        addrs[i] = TTOS_AllocVaddr(PAGE_SIZE, 16 * PAGE_SIZE);
        t1       = now_ns();

        TEST_ASSERT(addrs[i] != NULL);
        d      = t1 - t0;
        total += d;
        if (d < min_ns) min_ns = d;
        if (d > max_ns) max_ns = d;
    }

    printf("  aligned alloc x%d: min=%llu ns, max=%llu ns, avg=%llu ns\n",
           S4_COUNT,
           (unsigned long long)min_ns,
           (unsigned long long)max_ns,
           (unsigned long long)(total / S4_COUNT));

    TEST_ASSERT(total > 0);

    for (i = 0; i < S4_COUNT; i++)
    {
        TTOS_FreeVaddr(addrs[i], PAGE_SIZE);
    }

    TTOS_VaddrArenaDestroy();
}

int main(void)
{
    test_latency_sequential_alloc();
    test_latency_fragmented_alloc();
    test_latency_free_coalesce();
    test_latency_aligned_alloc();

    TEST_SUITE_RESULTS();
}
