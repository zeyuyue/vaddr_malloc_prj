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
 * @file:  ttosVaddrArena.c
 * @brief:
 *       <li>虚拟地址 arena 生命周期管理及空闲段链表操作。</li>
 */

/*
 * @brief:
 *       <li>该模块实现全局 arena 的创建与销毁，以及维护有序空闲段双向链表
 *           所需的插入、删除、合并等辅助操作。</li>
 *       <li>节点池在初始化时一次性分配，关键路径内不再调用堆分配器。
 *           注意：节点取用/归还为 O(1)，但段链表遍历（查找、插入）
 *           为 O(空闲段数)，最坏 O(total_pages/2)。</li>
 */

/************************头 文 件******************************/
#include <stdlib.h>
#include <string.h>
#include "ttosVaddrInternal.h"
/************************宏 定 义******************************/
/*
 * 节点池容量 = total_pages / 2 + 2。
 * 最坏情况：页交替占用时最多产生 total_pages/2 个不连续空闲段，
 * 额外 +2 保留初始整段节点及合并过程中的临时余量。
 */
#define NODE_POOL_SIZE(total_pages)  ((total_pages) / 2u + 2u)
/************************类型定义******************************/
/************************外部声明******************************/
/************************前向声明******************************/
/************************模块变量******************************/
/************************全局变量******************************/
arena_t g_vaddr_arena;
/************************实   现*******************************/

/**
 * @brief 从 arena 节点池取出一个空闲节点并初始化。
 *        在持锁路径中调用，执行时间 O(1)，不调用堆分配器。
 *
 * @param[in,out]  arena       所属 arena（节点从其节点池取出）。
 * @param[in]      start_page  空闲段起始页索引。
 * @param[in]      page_count  空闲段连续页数。
 * @return 节点指针，节点池耗尽时返回 NULL。
 */
free_seg_t *seg_node_alloc(arena_t *arena, size_t start_page, size_t page_count)
{
    free_seg_t *node = arena->node_free_list;
    if (!node)
        return NULL;

    arena->node_free_list = node->next;
    node->start_page = start_page;
    node->page_count = page_count;
    node->prev       = NULL;
    node->next       = NULL;
    return node;
}

/**
 * @brief 将节点归还 arena 节点池（LIFO）。
 *        在持锁路径中调用，执行时间 O(1)，不调用堆分配器。
 *
 * @param[in,out]  arena  所属 arena。
 * @param[in]      node   待归还的节点，传入 NULL 时为空操作。
 */
void seg_node_free(arena_t *arena, free_seg_t *node)
{
    if (!node)
        return;
    node->next = arena->node_free_list;
    node->prev = NULL;
    arena->node_free_list = node;
}

/**
 * @brief 将节点插入 arena 的有序空闲段链表（按 start_page 升序）。
 *
 * @param[in,out]  arena  目标 arena。
 * @param[in]      node   待插入节点（不得已在链表中）。
 */
void seg_insert(arena_t *arena, free_seg_t *node)
{
    free_seg_t *cur  = arena->seg_list;
    free_seg_t *prev = NULL;

    /* 找到第一个 start_page >= node->start_page 的位置 */
    while (cur && cur->start_page < node->start_page)
    {
        prev = cur;
        cur  = cur->next;
    }

    node->prev = prev;
    node->next = cur;

    if (prev)
        prev->next = node;
    else
        arena->seg_list = node;

    if (cur)
        cur->prev = node;
}

/**
 * @brief 从 arena 的空闲段链表中摘除节点。
 *        节点内部指针置零，由调用方负责归还节点池。
 *
 * @param[in,out]  arena  所属 arena。
 * @param[in]      node   待摘除节点（须在链表中）。
 */
void seg_remove(arena_t *arena, free_seg_t *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        arena->seg_list = node->next;

    if (node->next)
        node->next->prev = node->prev;

    node->prev = NULL;
    node->next = NULL;
}

/**
 * @brief 将 node 与前后相邻节点合并（若页范围连续）。
 *        合并过程中 node 可能被归还节点池，调用后不得再访问。
 *
 * @param[in,out]  arena  所属 arena。
 * @param[in,out]  node   刚插入链表的空闲段节点。
 */
void seg_merge_adjacent(arena_t *arena, free_seg_t *node)
{
    /* 尝试与后继节点合并 */
    if (node->next &&
        node->start_page + node->page_count == node->next->start_page)
    {
        free_seg_t *next = node->next;
        node->page_count += next->page_count;
        seg_remove(arena, next);
        seg_node_free(arena, next);
    }

    /* 尝试与前驱节点合并，合并成功后 node 被消耗，不可再使用 */
    if (node->prev &&
        node->prev->start_page + node->prev->page_count == node->start_page)
    {
        free_seg_t *prev = node->prev;
        prev->page_count += node->page_count;
        seg_remove(arena, node);
        seg_node_free(arena, node);
    }
}

/**
 * @brief 释放 arena 内部已分配的资源并将结构体清零。
 *        仅供 TTOS_VaddrArenaInit 初始化失败时的内部清理使用，
 *        调用方须确保 mutex 已初始化时才传 mutex_inited = 1。
 *
 * @param[in,out]  arena         待清理的 arena。
 * @param[in]      mutex_inited  mutex 是否已成功初始化（1：是，0：否）。
 */
static void arena_cleanup(arena_t *arena, int mutex_inited)
{
    if (mutex_inited)
        pthread_mutex_destroy(&arena->lock);
    free(arena->node_pool);
    free(arena->bitmap);
    memset(arena, 0, sizeof(arena_t));
}

/**
 * @brief 初始化全局虚拟地址 arena，管理 [base, base+size) 区间。
 *        base 和 size 均须按 PAGE_SIZE（4096）对齐。
 *        base 不得为 0（首页地址等于 NULL，与失败返回值冲突），
 *        且 base + size 不得溢出 uintptr_t。
 *        初始化时一次性分配节点池，后续关键路径不再调用堆分配器。
 *
 * @param[in]  base  被管理区间的起始虚拟地址（须 > 0 且页对齐）。
 * @param[in]  size  区间总字节数（须页对齐，且 base+size 不得溢出）。
 * @return 成功返回 TTOS_OK；
 *         size 为 0 返回 TTOS_INVALID_SIZE；
 *         base 或 size 未页对齐返回 TTOS_INVALID_ALIGNED；
 *         base 为 0 或 base+size 溢出返回 TTOS_INVALID_ADDRESS；
 *         内存不足返回 TTOS_UNSATISFIED；
 *         内部资源初始化失败返回 TTOS_INTERNAL_ERROR。
 */
T_TTOS_ReturnCode TTOS_VaddrArenaInit(uintptr_t base, size_t size)
{
    if (size == 0)
        return TTOS_INVALID_SIZE;

    if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)))
        return TTOS_INVALID_ALIGNED;

    /* base + size 不得回绕 uintptr_t，否则高地址页的虚拟地址会溢出为 0 */
    /* base 为 0 时首页地址 == NULL，与分配失败返回值冲突，须拒绝 */
    if (base == 0 || base + size < base)
        return TTOS_INVALID_ADDRESS;

    /* 若已初始化则先清理，支持重新初始化 */
    if (g_vaddr_arena.bitmap)
        TTOS_VaddrArenaDestroy();

    g_vaddr_arena.base         = base;
    g_vaddr_arena.total_pages  = size >> PAGE_SHIFT;
    g_vaddr_arena.bitmap_bytes = (g_vaddr_arena.total_pages + 7u) / 8u;
    g_vaddr_arena.free_pages   = g_vaddr_arena.total_pages;

    g_vaddr_arena.bitmap = calloc(g_vaddr_arena.bitmap_bytes, 1);
    if (!g_vaddr_arena.bitmap)
    {
        memset(&g_vaddr_arena, 0, sizeof(arena_t));
        return TTOS_UNSATISFIED;
    }

    /* 预分配节点池：关键路径内取用/归还段节点均为 O(1)，不调用堆分配器 */
    g_vaddr_arena.node_pool_size = NODE_POOL_SIZE(g_vaddr_arena.total_pages);
    g_vaddr_arena.node_pool      = calloc(g_vaddr_arena.node_pool_size,
                                          sizeof(free_seg_t));
    if (!g_vaddr_arena.node_pool)
    {
        arena_cleanup(&g_vaddr_arena, 0);
        return TTOS_UNSATISFIED;
    }

    /* 将所有节点串成空闲链表 */
    for (size_t i = 0; i < g_vaddr_arena.node_pool_size - 1u; i++)
        g_vaddr_arena.node_pool[i].next = &g_vaddr_arena.node_pool[i + 1u];
    g_vaddr_arena.node_pool[g_vaddr_arena.node_pool_size - 1u].next = NULL;
    g_vaddr_arena.node_free_list = &g_vaddr_arena.node_pool[0];

    if (pthread_mutex_init(&g_vaddr_arena.lock, NULL) != 0)
    {
        arena_cleanup(&g_vaddr_arena, 0);
        return TTOS_INTERNAL_ERROR;
    }

    /* 初始状态：整个 arena 是一个连续的空闲段 */
    free_seg_t *initial = seg_node_alloc(&g_vaddr_arena, 0, g_vaddr_arena.total_pages);
    if (!initial)
    {
        arena_cleanup(&g_vaddr_arena, 1);
        return TTOS_INTERNAL_ERROR;
    }

    g_vaddr_arena.seg_list = initial;
    return TTOS_OK;
}

/**
 * @brief 销毁全局 arena 并释放所有内部元数据（位图、节点池）。
 *        不会释放或解除映射调用者已分配但未归还的虚拟地址范围。
 */
void TTOS_VaddrArenaDestroy(void)
{
    if (!g_vaddr_arena.bitmap)
        return;

    pthread_mutex_lock(&g_vaddr_arena.lock);
    g_vaddr_arena.seg_list       = NULL;
    g_vaddr_arena.node_free_list = NULL;
    /* 持锁期间将 bitmap/node_pool 指针置 NULL 并保存到局部变量。
     * 解锁后其他线程检查 bitmap == NULL 将立即退出，
     * 不会再尝试加锁已销毁的 mutex，避免 UB。 */
    uint8_t    *bm = g_vaddr_arena.bitmap;
    free_seg_t *np = g_vaddr_arena.node_pool;
    g_vaddr_arena.bitmap    = NULL;
    g_vaddr_arena.node_pool = NULL;
    pthread_mutex_unlock(&g_vaddr_arena.lock);

    pthread_mutex_destroy(&g_vaddr_arena.lock);

    free(np);
    free(bm);
    memset(&g_vaddr_arena, 0, sizeof(arena_t));
}

/**
 * @brief 查询全局 arena 当前统计信息。
 *
 * @param[out]  stats  接收当前页计数的结构体。
 * @return 成功返回 TTOS_OK；
 *         stats 为 NULL 返回 TTOS_INVALID_ADDRESS；
 *         arena 未初始化返回 TTOS_INVALID_STATE。
 */
T_TTOS_ReturnCode TTOS_VaddrArenaStats(TTOS_VaddrStats *stats)
{
    if (!stats)
        return TTOS_INVALID_ADDRESS;

    if (!g_vaddr_arena.bitmap)
        return TTOS_INVALID_STATE;

    pthread_mutex_lock(&g_vaddr_arena.lock);
    stats->total_pages = g_vaddr_arena.total_pages;
    stats->free_pages  = g_vaddr_arena.free_pages;
    stats->used_pages  = g_vaddr_arena.total_pages - g_vaddr_arena.free_pages;
    pthread_mutex_unlock(&g_vaddr_arena.lock);

    return TTOS_OK;
}
