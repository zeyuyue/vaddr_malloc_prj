#include <stdint.h>
#include <stddef.h>
#include "ttosVaddrMalloc.h"
#include "ttosTestFramework.h"

#define ARENA_BASE  0x10000000UL
#define ARENA_PAGES 256U
#define ARENA_SIZE  (ARENA_PAGES * 4096U)
#define PAGE_SIZE   4096U

/* addr 是否落在 arena 管理范围内 */
static int in_arena(void *addr, size_t size)
{
    uintptr_t a   = (uintptr_t)addr;
    uintptr_t end = a + size;
    return a >= ARENA_BASE && end <= ARENA_BASE + ARENA_SIZE;
}

static void test_init_destroy(void)
{
    TEST_SUITE_BEGIN("init/destroy");

    /* 正常初始化 */
    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE) == TTOS_OK);

    TTOS_VaddrStats stats;
    TEST_ASSERT(TTOS_VaddrArenaStats(&stats) == TTOS_OK);
    TEST_ASSERT(stats.total_pages == ARENA_PAGES);
    TEST_ASSERT(stats.free_pages  == ARENA_PAGES);
    TEST_ASSERT(stats.used_pages  == 0);

    TTOS_VaddrArenaDestroy();

    /* 基地址未对齐 */
    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE + 1, ARENA_SIZE) != TTOS_OK);

    /* 大小未对齐 */
    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE + 1) != TTOS_OK);

    /* 大小为零 */
    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE, 0) != TTOS_OK);

    /* base 为 0：首页地址 == NULL，与分配失败返回值冲突，须拒绝 */
    TEST_ASSERT(TTOS_VaddrArenaInit(0, ARENA_SIZE) != TTOS_OK);

    /* base + size 回绕溢出（32/64 位均测试最大对齐地址） */
    uintptr_t max_page_aligned = ~(uintptr_t)0 - PAGE_SIZE + 1; /* 最高页对齐地址 */
    TEST_ASSERT(TTOS_VaddrArenaInit(max_page_aligned, 2 * PAGE_SIZE) != TTOS_OK);

    /* 重新初始化应能成功 */
    TEST_ASSERT(TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE) == TTOS_OK);
    TTOS_VaddrArenaDestroy();
}

static void test_single_alloc_free(void)
{
    TEST_SUITE_BEGIN("single alloc/free");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    void *addr = TTOS_AllocVaddr(PAGE_SIZE);
    TEST_ASSERT(addr != NULL);
    TEST_ASSERT((uintptr_t)addr % PAGE_SIZE == 0);
    TEST_ASSERT(in_arena(addr, PAGE_SIZE));

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.used_pages == 1);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES - 1);

    TTOS_FreeVaddr(addr, PAGE_SIZE);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);
    TEST_ASSERT(stats.used_pages == 0);

    TTOS_VaddrArenaDestroy();
}

static void test_multi_alloc(void)
{
    TEST_SUITE_BEGIN("multiple small allocs");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

#define N 8
    void *addrs[N];
    for (int i = 0; i < N; i++)
    {
        addrs[i] = TTOS_AllocVaddr(PAGE_SIZE);
        TEST_ASSERT(addrs[i] != NULL);
        TEST_ASSERT(in_arena(addrs[i], PAGE_SIZE));
    }

    /* 所有地址必须唯一且页对齐 */
    for (int i = 0; i < N; i++)
    {
        TEST_ASSERT((uintptr_t)addrs[i] % PAGE_SIZE == 0);
        for (int j = i + 1; j < N; j++)
            TEST_ASSERT(addrs[i] != addrs[j]);
    }

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.used_pages == N);

    for (int i = 0; i < N; i++)
        TTOS_FreeVaddr(addrs[i], PAGE_SIZE);

    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);
#undef N

    TTOS_VaddrArenaDestroy();
}

static void test_large_alloc(void)
{
    TEST_SUITE_BEGIN("large alloc (>= 16 pages)");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /* 32 页分配，触发大块路径 */
    size_t large = 32 * PAGE_SIZE;
    void *addr = TTOS_AllocVaddr(large);
    TEST_ASSERT(addr != NULL);
    TEST_ASSERT((uintptr_t)addr % PAGE_SIZE == 0);
    TEST_ASSERT(in_arena(addr, large));

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.used_pages == 32);

    TTOS_FreeVaddr(addr, large);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

static void test_size_rounding(void)
{
    TEST_SUITE_BEGIN("size rounding to page boundary");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /* 请求 1 字节，内部应向上取整为 1 页 */
    void *addr = TTOS_AllocVaddr(1);
    TEST_ASSERT(addr != NULL);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.used_pages == 1);

    TTOS_FreeVaddr(addr, 1);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

static void test_oom(void)
{
    TEST_SUITE_BEGIN("OOM - request exceeds available pages");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /* 耗尽 arena */
    void *full = TTOS_AllocVaddr(ARENA_SIZE);
    TEST_ASSERT(full != NULL);

    /* 再分配必须失败 */
    void *extra = TTOS_AllocVaddr(PAGE_SIZE);
    TEST_ASSERT(extra == NULL);

    TTOS_FreeVaddr(full, ARENA_SIZE);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

static void test_fragmentation_reuse(void)
{
    TEST_SUITE_BEGIN("fragmentation and coalesce");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    void *a = TTOS_AllocVaddr(4 * PAGE_SIZE);
    void *b = TTOS_AllocVaddr(4 * PAGE_SIZE);
    void *c = TTOS_AllocVaddr(4 * PAGE_SIZE);
    void *d = TTOS_AllocVaddr(4 * PAGE_SIZE);
    TEST_ASSERT(a && b && c && d);

    /* 释放交替块制造碎片 */
    TTOS_FreeVaddr(a, 4 * PAGE_SIZE);
    TTOS_FreeVaddr(c, 4 * PAGE_SIZE);

    /* 能够重新分配到已释放的空洞 */
    void *e = TTOS_AllocVaddr(4 * PAGE_SIZE);
    void *f = TTOS_AllocVaddr(4 * PAGE_SIZE);
    TEST_ASSERT(e != NULL);
    TEST_ASSERT(f != NULL);

    TTOS_FreeVaddr(b, 4 * PAGE_SIZE);
    TTOS_FreeVaddr(d, 4 * PAGE_SIZE);
    TTOS_FreeVaddr(e, 4 * PAGE_SIZE);
    TTOS_FreeVaddr(f, 4 * PAGE_SIZE);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

static void test_null_guards(void)
{
    TEST_SUITE_BEGIN("NULL/invalid argument guards");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /* 零大小分配应返回 NULL */
    TEST_ASSERT(TTOS_AllocVaddr(0) == NULL);

    /* 释放 NULL 地址不应崩溃 */
    TTOS_FreeVaddr(NULL, PAGE_SIZE);

    /* stats 传 NULL 应返回 -1 */
    TEST_ASSERT(TTOS_VaddrArenaStats(NULL) != TTOS_OK);

    TTOS_VaddrArenaDestroy();

    /* 销毁后再调用不应崩溃 */
    TTOS_VaddrArenaDestroy();
    TEST_ASSERT(TTOS_AllocVaddr(PAGE_SIZE) == NULL);
    TEST_ASSERT(TTOS_VaddrArenaStats(NULL)  != TTOS_OK);
}

static void test_double_free_basic(void)
{
    TEST_SUITE_BEGIN("double free - basic detection");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    void *a = TTOS_AllocVaddr(PAGE_SIZE);
    TEST_ASSERT(a != NULL);

    TTOS_FreeVaddr(a, PAGE_SIZE);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    size_t free_after_first = stats.free_pages;

    /* 双重释放：应被检测到，free_pages 不应变化 */
    TTOS_FreeVaddr(a, PAGE_SIZE);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == free_after_first);

    TTOS_VaddrArenaDestroy();
}

static void test_double_free_partial_overlap(void)
{
    TEST_SUITE_BEGIN("double free - partial overlap detection");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /*
     * 复现 Bug 2 场景：
     * 1. 分配 2 页（a: page 0-1）
     * 2. 释放 a
     * 3. 分配 1 页（b: page 0，被重新分配）
     * 4. 对 a 做双重释放 — 首页已被 b 占用(bit=1)，
     *    但第二页已空闲(bit=0)，全页校验应拒绝
     */
    void *a = TTOS_AllocVaddr(2 * PAGE_SIZE);
    TEST_ASSERT(a != NULL);

    TTOS_FreeVaddr(a, 2 * PAGE_SIZE);

    void *b = TTOS_AllocVaddr(PAGE_SIZE);
    TEST_ASSERT(b != NULL);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    size_t free_before = stats.free_pages;

    /* 对 a 做双重释放，应被拒绝 */
    TTOS_FreeVaddr(a, 2 * PAGE_SIZE);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == free_before);

    /* b 的分配应未受影响，正常释放后全部归还 */
    TTOS_FreeVaddr(b, PAGE_SIZE);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

static void test_free_misaligned_addr(void)
{
    TEST_SUITE_BEGIN("free with misaligned/out-of-range address");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    void *a = TTOS_AllocVaddr(PAGE_SIZE);
    TEST_ASSERT(a != NULL);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    size_t free_before = stats.free_pages;

    /* 未对齐地址：应静默忽略 */
    TTOS_FreeVaddr((void *)((uintptr_t)a + 1), PAGE_SIZE);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == free_before);

    /* 超出 arena 范围的地址：应静默忽略 */
    TTOS_FreeVaddr((void *)(ARENA_BASE + ARENA_SIZE), PAGE_SIZE);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == free_before);

    /* arena 之前的地址：应静默忽略 */
    TTOS_FreeVaddr((void *)(ARENA_BASE - PAGE_SIZE), PAGE_SIZE);
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == free_before);

    TTOS_FreeVaddr(a, PAGE_SIZE);
    TTOS_VaddrArenaDestroy();
}

static void test_exhaust_and_recover(void)
{
    TEST_SUITE_BEGIN("exhaust all pages then recover");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /* 逐页分配直到耗尽 */
    void *addrs[ARENA_PAGES];
    size_t allocated = 0;
    for (size_t i = 0; i < ARENA_PAGES; i++)
    {
        addrs[i] = TTOS_AllocVaddr(PAGE_SIZE);
        if (!addrs[i])
            break;
        allocated++;
    }
    TEST_ASSERT(allocated == ARENA_PAGES);

    /* 耗尽后再分配必须失败 */
    TEST_ASSERT(TTOS_AllocVaddr(PAGE_SIZE) == NULL);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == 0);

    /* 逐页释放 */
    for (size_t i = 0; i < allocated; i++)
        TTOS_FreeVaddr(addrs[i], PAGE_SIZE);

    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    /* 释放后应能重新分配整个 arena */
    void *full = TTOS_AllocVaddr(ARENA_SIZE);
    TEST_ASSERT(full != NULL);
    TTOS_FreeVaddr(full, ARENA_SIZE);

    TTOS_VaddrArenaDestroy();
}

static void test_coalesce_three_way(void)
{
    TEST_SUITE_BEGIN("three-way coalesce on free");

    TTOS_VaddrArenaInit(ARENA_BASE, ARENA_SIZE);

    /* 分配三个相邻块 */
    void *a = TTOS_AllocVaddr(4 * PAGE_SIZE);
    void *b = TTOS_AllocVaddr(4 * PAGE_SIZE);
    void *c = TTOS_AllocVaddr(4 * PAGE_SIZE);
    TEST_ASSERT(a && b && c);

    /* 先释放两端 */
    TTOS_FreeVaddr(a, 4 * PAGE_SIZE);
    TTOS_FreeVaddr(c, 4 * PAGE_SIZE);

    /* 释放中间块，应触发三段合并 */
    TTOS_FreeVaddr(b, 4 * PAGE_SIZE);

    /* 合并后应能一次性分配 12 页 */
    void *big = TTOS_AllocVaddr(12 * PAGE_SIZE);
    TEST_ASSERT(big != NULL);

    TTOS_FreeVaddr(big, 12 * PAGE_SIZE);

    TTOS_VaddrStats stats;
    TTOS_VaddrArenaStats(&stats);
    TEST_ASSERT(stats.free_pages == ARENA_PAGES);

    TTOS_VaddrArenaDestroy();
}

int main(void)
{
    test_init_destroy();
    test_single_alloc_free();
    test_multi_alloc();
    test_large_alloc();
    test_size_rounding();
    test_oom();
    test_fragmentation_reuse();
    test_null_guards();
    test_double_free_basic();
    test_double_free_partial_overlap();
    test_free_misaligned_addr();
    test_exhaust_and_recover();
    test_coalesce_three_way();

    TEST_SUITE_RESULTS();
}
