/************************************************************************************************
 *              科东(广州)软件科技有限公司 版权所有
 *   Copyright (C) 2021 Intewell (Guangzhou) Software Technology Co., Ltd. All Rights Reserved.
 *************************************************************************************************/

/*
 * 修改历史：
 * 2026-04-07     岳泽宇，科东(广州)软件科技有限公司
 *               创建该文件。
 */

/*
 * @file:  ttosVaddrAlloc.c
 * @brief:
 *       <li>虚拟地址空间分配与释放实现。</li>
 */

/*
 * @brief:
 *       <li>该模块通过段链表首次适配完成虚拟地址分配，位图作为页状态的
 *           权威记录（用于双重释放检测及状态追踪），不参与分配路径搜索。</li>
 *       <li>关键路径内不调用堆分配器；节点取用/归还为 O(1)，
 *           段链表搜索和插入为 O(空闲段数)。</li>
 */

/************************头 文 件******************************/
#include "ttosVaddrInternal.h"
/************************宏 定 义******************************/
/************************类型定义******************************/
/************************外部声明******************************/
/************************前向声明******************************/
/************************模块变量******************************/
/************************全局变量******************************/
/************************实   现*******************************/

/**
 * @brief 将字节数向上取整到页边界，返回所需页数。
 *        使用无溢出写法，在 32/64 位系统均安全。
 *        调用方须保证 size >= 1。
 *
 * @param[in]  size  请求字节数（须 >= 1）。
 * @return 覆盖 size 所需的最少页数。
 */
static size_t size_to_pages(size_t size)
{
    /* 等价于 ceil(size / PAGE_SIZE)，避免 size + PAGE_SIZE - 1 在极大值时溢出 */
    return ((size - 1u) >> PAGE_SHIFT) + 1u;
}

/**
 * @brief 空闲段链表对齐适配搜索：找到第一个能在 align_pages 对齐约束下
 *        容纳 n 页的空闲段，并输出段内对齐起始页索引。
 *
 * @param[in]   arena        所属 arena（用于计算绝对地址对齐）。
 * @param[in]   n            所需最少页数。
 * @param[in]   align_pages  对齐粒度（页数，须 >= 1）。
 * @param[out]  out_start    满足对齐要求的起始页索引。
 * @return 满足条件的第一个段节点，不存在时返回 NULL。
 */
static free_seg_t *seg_find_fit_aligned(arena_t *arena, size_t n, size_t align_pages,
                                        size_t *out_start)
{
    size_t base_pages = arena->base >> PAGE_SHIFT;

    for (free_seg_t *s = arena->seg_list; s; s = s->next)
    {
        /* 计算段内第一个满足对齐要求的起始页 */
        size_t offset = (base_pages + s->start_page) % align_pages;
        size_t p      = s->start_page + (offset == 0u ? 0u : align_pages - offset);

        if (p + n <= s->start_page + s->page_count)
        {
            *out_start = p;
            return s;
        }
    }
    return NULL;
}

/**
 * @brief 将位图中 [start, start+count) 范围的页标记为已分配。
 *
 * @param[in,out]  bitmap  待更新的页位图。
 * @param[in]      start   起始页索引。
 * @param[in]      count   页数。
 */
static void bitmap_mark_used(uint8_t *bitmap, size_t start, size_t count)
{
    for (size_t i = start; i < start + count; i++)
    {
        bitmap_set(bitmap, i);
    }
}

/**
 * @brief 将位图中 [start, start+count) 范围的页标记为空闲。
 *
 * @param[in,out]  bitmap  待更新的页位图。
 * @param[in]      start   起始页索引。
 * @param[in]      count   页数。
 */
static void bitmap_mark_free(uint8_t *bitmap, size_t start, size_t count)
{
    for (size_t i = start; i < start + count; i++)
    {
        bitmap_clear(bitmap, i);
    }
}

/**
 * @brief 从段的任意对齐位置截取 n 页，前后余量各自保留为独立空闲段。
 *        先预取后部节点以保证操作原子性：若节点池耗尽则放弃本次操作，
 *        位图与 seg_list 均不会被修改。
 *
 * @param[in,out]  arena       所属 arena。
 * @param[in,out]  seg         被截取的段节点（须在链表中）。
 * @param[in]      start_page  截取起始页（须在 seg 范围内）。
 * @param[in]      n           截取页数。
 * @return 0 成功；-1 节点池耗尽，状态未被修改。
 */
static int seg_carve_aligned(arena_t *arena, free_seg_t *seg,
                              size_t start_page, size_t n)
{
    size_t seg_end     = seg->start_page + seg->page_count;
    size_t front_pages = start_page - seg->start_page;
    size_t back_pages  = seg_end - (start_page + n);
    free_seg_t *back   = NULL;

    /* 预取后部节点，保证后续操作不会因节点耗尽而中途失败 */
    if (back_pages > 0)
    {
        back = seg_node_alloc(arena, start_page + n, back_pages);
        if (!back)
        {
            return -1;
        }
    }

    seg_remove(arena, seg);

    if (front_pages > 0)
    {
        /* 复用 seg 节点保留前部余量 */
        seg->page_count = front_pages;
        seg->prev       = NULL;
        seg->next       = NULL;
        seg_insert(arena, seg);
    }
    else
    {
        seg_node_free(arena, seg);
    }

    if (back)
    {
        seg_insert(arena, back);
    }

    return 0;
}

/**
 * @brief 在全局 arena 中分配至少 size 字节的连续虚拟地址范围。
 *        返回地址满足 align 对齐要求；size 在内部向上取整到页边界。
 *        align 为 0 或等于 PAGE_SIZE 时退化为默认页对齐。
 *        align 非零时须为 PAGE_SIZE 的整数倍且为 2 的幂次，否则返回 NULL。
 *
 * @param[in]  size   请求字节数（须大于 0，内部向上取整到页边界）。
 * @param[in]  align  返回地址的对齐字节数（须为 PAGE_SIZE 的 2 幂次倍，0 表示默认）。
 * @return 分配到的虚拟地址起始值（满足对齐），失败返回 NULL。
 */
void *TTOS_AllocVaddr(size_t size, size_t align)
{
    arena_t *arena = &g_vaddr_arena;

    if (!arena->bitmap || size == 0)
    {
        return NULL;
    }

    /* align 非零时须为 PAGE_SIZE 的整数倍且为 2 的幂次；
     * 违反时直接返回 NULL，不做静默退化以免掩盖调用方错误。 */
    if (align != 0 && (align < PAGE_SIZE || (align & (align - 1)) != 0))
    {
        return NULL;
    }

    size_t n_pages     = size_to_pages(size);
    /* align 为 0 或等于 PAGE_SIZE 时使用默认页对齐（align_pages = 1） */
    size_t align_pages = (align <= PAGE_SIZE) ? 1u : (align >> PAGE_SHIFT);

    pthread_mutex_lock(&arena->lock);

    /* 快速 OOM 检查 */
    if (n_pages > arena->free_pages)
    {
        pthread_mutex_unlock(&arena->lock);
        return NULL;
    }

    size_t start_page;
    free_seg_t *seg = seg_find_fit_aligned(arena, n_pages, align_pages, &start_page);
    if (!seg)
    {
        pthread_mutex_unlock(&arena->lock);
        return NULL;
    }

    if (seg_carve_aligned(arena, seg, start_page, n_pages) != 0)
    {
        pthread_mutex_unlock(&arena->lock);
        return NULL;
    }

    bitmap_mark_used(arena->bitmap, start_page, n_pages);
    arena->free_pages -= n_pages;

    pthread_mutex_unlock(&arena->lock);

    return (void *)(arena->base + (start_page << PAGE_SHIFT));
}

/**
 * @brief 释放之前由 TTOS_AllocVaddr 分配的虚拟地址范围。
 *        addr 和 size 须与分配时完全一致，释放后自动合并相邻空闲段。
 *        检测双重释放：范围内任意页位图已为空闲时返回 TTOS_FAIL。
 *        先取节点再更新状态，保证节点池耗尽时状态始终一致。
 *
 * @param[in]  addr  TTOS_AllocVaddr 返回的地址。
 * @param[in]  size  分配时传入的 size。
 * @return 成功返回 TTOS_OK；
 *         arena 未初始化返回 TTOS_INVALID_STATE；
 *         addr 为 NULL 或地址非法返回 TTOS_INVALID_ADDRESS；
 *         size 为 0 返回 TTOS_INVALID_SIZE；
 *         检测到双重释放返回 TTOS_FAIL；
 *         节点池耗尽（释放被放弃）返回 TTOS_UNSATISFIED。
 */
T_TTOS_ReturnCode TTOS_FreeVaddr(void *addr, size_t size)
{
    arena_t *arena = &g_vaddr_arena;

    if (!arena->bitmap)
    {
        return TTOS_INVALID_STATE;
    }

    if (!addr)
    {
        return TTOS_INVALID_ADDRESS;
    }

    if (size == 0)
    {
        return TTOS_INVALID_SIZE;
    }

    uintptr_t vaddr = (uintptr_t)addr;
    if (vaddr < arena->base || (vaddr & (PAGE_SIZE - 1)))
    {
        return TTOS_INVALID_ADDRESS;
    }

    size_t n_pages    = size_to_pages(size);
    size_t start_page = (vaddr - arena->base) >> PAGE_SHIFT;

    if (start_page + n_pages > arena->total_pages)
    {
        return TTOS_INVALID_ADDRESS;
    }

    pthread_mutex_lock(&arena->lock);

    /* 双重释放检测：范围内任意页已空闲则拒绝整次操作。           */
    /* 仅检查首页不够：首页可能被其他分配重新占用，从而绕过检测， */
    /* 导致其他活跃分配的位图被静默清零、seg_list 出现重叠段。   */
    for (size_t i = start_page; i < start_page + n_pages; i++)
    {
        if (!bitmap_get(arena->bitmap, i))
        {
            pthread_mutex_unlock(&arena->lock);
            return TTOS_FAIL;
        }
    }

    /* 先取节点，成功后再更新位图和计数，保证状态原子一致。                   */
    /* 节点池耗尽时放弃本次释放，避免 bitmap/free_pages 与 seg_list 失步。 */
    free_seg_t *node = seg_node_alloc(arena, start_page, n_pages);
    if (!node)
    {
        pthread_mutex_unlock(&arena->lock);
        return TTOS_UNSATISFIED;
    }

    bitmap_mark_free(arena->bitmap, start_page, n_pages);
    arena->free_pages += n_pages;
    seg_insert(arena, node);
    seg_merge_adjacent(arena, node);
    /* node 可能已在 seg_merge_adjacent 内部归还节点池，此后不得再访问 */

    pthread_mutex_unlock(&arena->lock);
    return TTOS_OK;
}
