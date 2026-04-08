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
 * @file:  ttosVaddrInternal.h
 * @brief:
 *       <li>虚拟地址空间分配器内部类型、常量及位图操作定义。</li>
 */

/*
 * @brief:
 *       <li>本头文件仅供 src/ 内部模块使用，不属于对外公共接口，
 *           请勿在用户代码中直接包含。</li>
 */

#ifndef TTOS_VADDR_INTERNAL_H
#define TTOS_VADDR_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "ttosVaddrMalloc.h"

/* -----------------------------------------------------------------------
 * 常量
 * --------------------------------------------------------------------- */

#define PAGE_SIZE         4096U   /* 页大小，单位字节                      */
#define PAGE_SHIFT        12U     /* log2(PAGE_SIZE)，用于移位换算         */


/* -----------------------------------------------------------------------
 * 空闲段节点
 *
 * 表示一段连续的空闲页。节点按 start_page 升序存放于双向链表中，
 * 以便在释放时以 O(1) 完成相邻段合并。
 * --------------------------------------------------------------------- */
typedef struct free_seg
{
    size_t           start_page; /* 空闲段起始页索引                      */
    size_t           page_count; /* 连续空闲页数                          */
    struct free_seg *prev;
    struct free_seg *next;
} free_seg_t;

/* -----------------------------------------------------------------------
 * Arena
 *
 * 管理一段连续虚拟地址区间 [base, base + total_pages * PAGE_SIZE)。
 *
 * 两级跟踪结构：
 *   bitmap    — 每页 1 bit（1=已分配），作为单页状态的唯一真值来源，
 *               小块分配时通过位图扫描定位空闲页。
 *   seg_list  — 按地址排序的空闲段链表，大块分配时首次适配，
 *               释放时 O(1) 合并相邻段。
 *
 * 两个结构在每次分配/释放时保持同步。
 *
 * 实时性说明：
 *   node_pool / node_free_list 在初始化时一次性分配所有段节点，
 *   关键路径（持锁内）取用/归还节点均为 O(1)，不调用堆分配器。
 *   注意：seg_list 的首次适配搜索和有序插入为 O(空闲段数)，
 *   最坏情况 O(total_pages/2)，非严格 O(1) 有界。
 *
 * 32/64 位兼容：
 *   所有地址使用 uintptr_t，所有长度/索引使用 size_t，
 *   不含任何与位宽相关的假设。
 * --------------------------------------------------------------------- */
typedef struct arena
{
    uintptr_t        base;           /* arena 起始虚拟地址                */
    size_t           total_pages;    /* 受管理的总页数                    */
    uint8_t         *bitmap;         /* 位图：每页 1 bit，1=已分配        */
    size_t           bitmap_bytes;   /* 位图占用字节数                    */
    free_seg_t      *seg_list;       /* 有序空闲段链表表头                */
    size_t           free_pages;     /* 当前空闲页数（快速 OOM 判断）     */
    pthread_mutex_t  lock;           /* 互斥锁，保护所有字段              */
    free_seg_t      *node_pool;      /* 段节点预分配数组首地址            */
    free_seg_t      *node_free_list; /* 空闲节点链表头（LIFO 栈）         */
    size_t           node_pool_size; /* 节点池容量                        */
} arena_t;

/* 全局 arena 实例，由 ttosVaddrArena.c 定义 */
extern arena_t g_vaddr_arena;

/* -----------------------------------------------------------------------
 * 位图内联操作
 * --------------------------------------------------------------------- */

/* 将第 page 页标记为已分配 */
static inline void bitmap_set(uint8_t *bm, size_t page)
{
    bm[page >> 3] |= (uint8_t)(1u << (page & 7u));
}

/* 将第 page 页标记为空闲 */
static inline void bitmap_clear(uint8_t *bm, size_t page)
{
    bm[page >> 3] &= (uint8_t)(~(1u << (page & 7u)));
}

/* 查询第 page 页是否已分配，返回非零表示已分配 */
static inline int bitmap_get(const uint8_t *bm, size_t page)
{
    return (bm[page >> 3] >> (page & 7u)) & 1;
}

/* -----------------------------------------------------------------------
 * 空闲段链表操作声明（实现位于 ttosVaddrArena.c）
 * --------------------------------------------------------------------- */

free_seg_t *seg_node_alloc(arena_t *arena, size_t start_page, size_t page_count);
void        seg_node_free(arena_t *arena, free_seg_t *node);
void        seg_insert(arena_t *arena, free_seg_t *node);
void        seg_remove(arena_t *arena, free_seg_t *node);

/*
 * seg_merge_adjacent — 将 node 与其前后相邻节点合并（若地址连续）。
 * node 在合并过程中可能被释放，调用后不得再访问。
 */
void seg_merge_adjacent(arena_t *arena, free_seg_t *node);

#endif /* TTOS_VADDR_INTERNAL_H */
