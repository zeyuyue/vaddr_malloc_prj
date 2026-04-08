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
 * @file:  ttosVaddrMalloc.h
 * @brief:
 *       <li>虚拟地址空间分配器对外公共接口声明。</li>
 */

/*
 * @brief:
 *       <li>该模块提供虚拟地址空间 arena 的初始化、销毁、分配与释放接口，
 *           供 RTOS 内核及驱动层调用。所有接口均操作内部全局 arena 实例。</li>
 */

#ifndef TTOS_VADDR_MALLOC_H
#define TTOS_VADDR_MALLOC_H

#include <stddef.h>
#include <stdint.h>

/* TTOS_VaddrArenaStats 返回的统计快照 */
typedef struct
{
    size_t total_pages; /* arena 总页数       */
    size_t free_pages;  /* 当前空闲页数       */
    size_t used_pages;  /* 当前已分配页数     */
} TTOS_VaddrStats;

/**
 * @brief 初始化全局虚拟地址 arena，管理 [base, base+size) 区间。
 *        base 和 size 均须按 PAGE_SIZE（4096）对齐。
 *        base 不得为 0，且 base + size 不得溢出 uintptr_t。
 *
 * @param[in]  base  被管理区间的起始虚拟地址（须 > 0 且页对齐）。
 * @param[in]  size  区间总字节数（须页对齐，且 base+size 不得溢出）。
 * @return 成功返回 0，参数非法或内存不足返回 -1。
 */
int TTOS_VaddrArenaInit(uintptr_t base, size_t size);

/**
 * @brief 销毁全局 arena 并释放所有内部元数据。
 *        不会释放或解除映射调用者已分配但未归还的虚拟地址范围。
 */
void TTOS_VaddrArenaDestroy(void);

/**
 * @brief 在全局 arena 中分配至少 size 字节的连续虚拟地址范围。
 *        返回地址按 PAGE_SIZE 对齐，size 在内部向上取整到页边界。
 *
 * @param[in]  size  请求字节数（须大于 0）。
 * @return 分配到的虚拟地址起始值（页对齐），失败返回 NULL。
 */
void *TTOS_AllocVaddr(size_t size);

/**
 * @brief 释放之前由 TTOS_AllocVaddr 分配的虚拟地址范围。
 *        addr 和 size 须与分配时完全一致。
 *
 * @param[in]  addr  TTOS_AllocVaddr 返回的地址。
 * @param[in]  size  分配时传入的 size。
 */
void TTOS_FreeVaddr(void *addr, size_t size);

/**
 * @brief 查询全局 arena 当前统计信息。
 *
 * @param[out]  stats  接收当前页计数的结构体。
 * @return 成功返回 0，stats 为 NULL 或 arena 未初始化返回 -1。
 */
int TTOS_VaddrArenaStats(TTOS_VaddrStats *stats);

#endif /* TTOS_VADDR_MALLOC_H */
