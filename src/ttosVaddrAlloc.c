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
 * @brief 空闲段链表首次适配：找到第一个页数 >= n 的空闲段。
 *
 * @param[in]  list  有序空闲段链表表头。
 * @param[in]  n     所需最少页数。
 * @return 满足条件的第一个段节点，不存在时返回 NULL。
 */
static free_seg_t *seg_find_fit(free_seg_t *list, size_t n)
{
    for (free_seg_t *s = list; s; s = s->next)
    {
        if (s->page_count >= n)
            return s;
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
        bitmap_set(bitmap, i);
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
        bitmap_clear(bitmap, i);
}

/**
 * @brief 从段头部截取 n 页。段被完全消耗时从链表移除并归还节点池，
 *        否则就地缩减 start_page 和 page_count。
 *
 * @param[in,out]  arena  所属 arena。
 * @param[in,out]  seg    被截取的段节点。
 * @param[in]      n      截取页数。
 * @return 截取区域的起始页索引。
 */
static size_t seg_carve_front(arena_t *arena, free_seg_t *seg, size_t n)
{
    size_t start = seg->start_page;

    if (seg->page_count == n)
    {
        seg_remove(arena, seg);
        seg_node_free(arena, seg);
    }
    else
    {
        seg->start_page += n;
        seg->page_count -= n;
    }

    return start;
}

/**
 * @brief 在全局 arena 中分配至少 size 字节的连续虚拟地址范围。
 *        通过段链表首次适配定位空闲区域，从段头部截取所需页数。
 *
 * @param[in]  size  请求字节数（内部向上取整到页边界）。
 * @return 分配到的虚拟地址起始值，失败返回 NULL。
 */
void *TTOS_MallocVaddr(size_t size)
{
    arena_t *arena = &g_vaddr_arena;

    if (!arena->bitmap || size == 0)
        return NULL;

    size_t n_pages = size_to_pages(size);

    pthread_mutex_lock(&arena->lock);

    /* 快速 OOM 检查 */
    if (n_pages > arena->free_pages)
    {
        pthread_mutex_unlock(&arena->lock);
        return NULL;
    }

    /* 段链表首次适配，统一处理所有大小的请求 */
    free_seg_t *seg = seg_find_fit(arena->seg_list, n_pages);
    if (!seg)
    {
        pthread_mutex_unlock(&arena->lock);
        return NULL;
    }

    size_t start_page = seg_carve_front(arena, seg, n_pages);
    bitmap_mark_used(arena->bitmap, start_page, n_pages);
    arena->free_pages -= n_pages;

    pthread_mutex_unlock(&arena->lock);

    return (void *)(arena->base + (start_page << PAGE_SHIFT));
}

/**
 * @brief 释放之前由 TTOS_MallocVaddr 分配的虚拟地址范围。
 *        addr 和 size 须与分配时完全一致，释放后自动合并相邻空闲段。
 *        检测双重释放：首页位图已为空闲时直接返回，不更新任何状态。
 *        先取节点再更新状态，保证节点池耗尽时状态始终一致。
 *
 * @param[in]  addr  TTOS_MallocVaddr 返回的地址。
 * @param[in]  size  分配时传入的 size。
 */
void TTOS_FreeVaddr(void *addr, size_t size)
{
    arena_t *arena = &g_vaddr_arena;

    if (!arena->bitmap || !addr || size == 0)
        return;

    uintptr_t vaddr = (uintptr_t)addr;
    if (vaddr < arena->base || (vaddr & (PAGE_SIZE - 1)))
        return;

    size_t n_pages    = size_to_pages(size);
    size_t start_page = (vaddr - arena->base) >> PAGE_SHIFT;

    if (start_page + n_pages > arena->total_pages)
        return;

    pthread_mutex_lock(&arena->lock);

    /* 双重释放检测：范围内任意页已空闲则拒绝整次操作。
     * 仅检查首页不够：首页可能被其他分配重新占用，从而绕过检测，
     * 导致其他活跃分配的位图被静默清零、seg_list 出现重叠段。 */
    for (size_t i = start_page; i < start_page + n_pages; i++)
    {
        if (!bitmap_get(arena->bitmap, i))
        {
            pthread_mutex_unlock(&arena->lock);
            return;
        }
    }

    /*
     * 先取节点，成功后再更新位图和计数，保证状态原子一致。
     * 节点池耗尽时放弃本次释放，避免 bitmap/free_pages 与 seg_list 失步。
     */
    free_seg_t *node = seg_node_alloc(arena, start_page, n_pages);
    if (!node)
    {
        pthread_mutex_unlock(&arena->lock);
        return;
    }

    bitmap_mark_free(arena->bitmap, start_page, n_pages);
    arena->free_pages += n_pages;
    seg_insert(arena, node);
    seg_merge_adjacent(arena, node);
    /* node 可能已在 seg_merge_adjacent 内部归还节点池，此后不得再访问 */

    pthread_mutex_unlock(&arena->lock);
}
