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
 * @file:  ttosVaddrAlloc.h
 * @brief:
 *       <li>虚拟地址空间分配器对外公共接口声明。</li>
 */

/*
 * @brief:
 *       <li>该模块提供虚拟地址空间 arena 的初始化、销毁、分配与释放接口，
 *           供 RTOS 内核及驱动层调用。所有接口均操作内部全局 arena 实例。</li>
 */

#ifndef TTOS_VADDR_ALLOC_H
#define TTOS_VADDR_ALLOC_H

#include <stddef.h>
#include <stdint.h>

/* 页大小与移位常量（对外公开，调用方需要据此进行对齐） */
#define TTOS_PAGE_SIZE    (4096U)
#define TTOS_PAGE_SHIFT   (12U)

/* 所有 TTOS_ 前缀公共 API 的统一返回码类型 */
typedef enum
{
    /* 操作成功 */
    TTOS_OK = 0,
    /* 操作失败 */
    TTOS_FAIL = 1,
    /* 无效ID */
    TTOS_INVALID_ID = 2,
    /* 无效名字 */
    TTOS_INVALID_NAME = 3,
    /* 无效地址 */
    TTOS_INVALID_ADDRESS = 4,
    /* 无效时间 */
    TTOS_INVALID_TIME = 5,
    /* 无效状态 */
    TTOS_INVALID_STATE = 6,
    /* 无效动作 */
    TTOS_INVALID_TYPE = 7,
    /* 没有请求到资源 */
    TTOS_UNSATISFIED = 8,
    /* 无效的用户 */
    TTOS_INVALID_USER = 9,
    /* 在中断处理程序中执行 */
    TTOS_CALLED_FROM_ISR = 10,
    /* 无效的大小 */
    TTOS_INVALID_SIZE = 11,
    /* 超时 */
    TTOS_TIMEOUT = 12,
    /* 内部错误 */
    TTOS_INTERNAL_ERROR = 13,
    /* 无效的对齐 */
    TTOS_INVALID_ALIGNED = 14,
    /* 数值无效 */
    TTOS_INVALID_NUMBER = 15,
    /* 消息太多 */
    TTOS_TOO_MANY = 16,
    /* 对象已经被删除了 */
    TTOS_OBJECT_WAS_DELETED = 17,
    /* 无效的属性 */
    TTOS_INVALID_ATTRIBUTE = 18,
    /* 无效的优先级 */
    TTOS_INVALID_PRIORITY = 19,
    /* 非资源拥有者 */
    TTOS_NOT_OWNER_OF_RESOURCE = 20,
    /* 资源正在被使用中 */
    TTOS_RESOURCE_IN_USE = 21,
    /* 对象版本不匹配 */
    TTOS_INVAILD_VERSION = 22,
    /* 操作被屏蔽 */
    TTOS_MASKED = 23,
    /* 无效的索引 */
    TTOS_INVALID_INDEX = 24,
    /* 无效的系统调用 */
    TTOS_INVALID_SYSCALL = 25,
    /* 不支持互斥信号量嵌套获取 */
    TTOS_MUTEX_NESTING_NOT_ALLOWED = 26,
    /* 尝试获取互斥信号量的任务优先级高于当前互斥信号量天花板优先级 */
    TTOS_MUTEX_CEILING_VIOLATED = 27,
    /* 互斥信号量嵌套层数超出最大允许值 */
    TTOS_MUTEX_NEST_OVERFLOW = 28,
    /* 对象已经被取消了 */
    TTOS_OBJECT_WAS_CANCELED = 29,
    /* 等待对象被信号中断 */
    TTOS_SIGNAL_INTR = 30,
} T_TTOS_ReturnCode;

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
 * @return 成功返回 TTOS_OK；
 *         size 为 0 返回 TTOS_INVALID_SIZE；
 *         base 或 size 未页对齐返回 TTOS_INVALID_ALIGNED；
 *         base 为 0 或 base+size 溢出返回 TTOS_INVALID_ADDRESS；
 *         内存不足返回 TTOS_UNSATISFIED；
 *         内部资源初始化失败返回 TTOS_INTERNAL_ERROR。
 */
T_TTOS_ReturnCode TTOS_VaddrArenaInit(uintptr_t base, size_t size);

/**
 * @brief 销毁全局 arena 并释放所有内部元数据。
 *        不会释放或解除映射调用者已分配但未归还的虚拟地址范围。
 */
void TTOS_VaddrArenaDestroy(void);

/**
 * @brief 在全局 arena 中分配至少 size 字节的连续虚拟地址范围。
 *        返回地址满足 align 对齐要求；size 在内部向上取整到页边界。
 *        align 为 0 或小于 PAGE_SIZE 时退化为默认页对齐。
 *
 * @param[in]  size   请求字节数（须大于 0）。
 * @param[in]  align  返回地址的对齐字节数（须为 PAGE_SIZE 的倍数，0 表示默认）。
 * @return 分配到的虚拟地址起始值（满足对齐），失败返回 NULL。
 */
void *TTOS_AllocVaddr(size_t size, size_t align);

/**
 * @brief 释放之前由 TTOS_AllocVaddr 分配的虚拟地址范围。
 *        addr 和 size 须与分配时完全一致。
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
T_TTOS_ReturnCode TTOS_FreeVaddr(void *addr, size_t size);

/**
 * @brief 查询全局 arena 当前统计信息。
 *
 * @param[out]  stats  接收当前页计数的结构体。
 * @return 成功返回 TTOS_OK；
 *         stats 为 NULL 返回 TTOS_INVALID_ADDRESS；
 *         arena 未初始化返回 TTOS_INVALID_STATE。
 */
T_TTOS_ReturnCode TTOS_VaddrArenaStats(TTOS_VaddrStats *stats);

#endif /* TTOS_VADDR_ALLOC_H */
