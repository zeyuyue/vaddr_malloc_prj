# CLAUDE.md

## 项目概述

`vaddr_malloc_prj` 是一个虚拟地址空间内存分配器项目，设计并实现自定义 `TTOS_AllocVaddr`/`TTOS_FreeVaddr` 及虚拟地址管理。

## 构建

```bash
make          # 构建
make test     # 运行测试
make clean    # 清理产物
```

## 目录结构

- `src/` — 分配器实现
- `include/` — 公共头文件
- `tests/` — 单元测试与集成测试
- `docs/` — 设计文档与参考资料

## 代码风格

### 基本规则

- 语言：C（C11 标准）
- 缩进：4 个空格，不使用 Tab
- 头文件保护：`#ifndef PROJECT_FILE_H` / `#define PROJECT_FILE_H` / `#endif`
- 函数保持简短、职责单一
- 偏好显式而非隐式，避免晦涩的技巧性写法
- 禁止使用 `goto` 语句
- 注释使用中文

### 文件命名与编码

- 文件名以 `ttos` 开头，采用小驼峰风格，例如：`ttosVaddrAlloc.c`、`ttosVaddrAlloc.h`
- 文件编码：UTF-8
- 换行符：CRLF（Windows 风格）

### 命名规范

- 对外公共 API：`TTOS_` 前缀 + 大驼峰，例如 `TTOS_AllocVaddr`
- 内部模块函数：小驼峰，例如 `vaddrAlloc`
- 局部变量：`snake_case`，例如 `block_size`
- 宏和常量：`UPPER_CASE`，例如 `PAGE_SIZE_4K`
- 结构体：tag 与 typedef 均使用 `snake_case`，typedef 以 `_t` 结尾

```c
typedef struct rb_node
{
    int key;
    struct rb_node *left, *right, *parent;
} rb_node_t;
```

### 函数返回码风格
TTOS_前缀的给应用的函数使用该套返回码
```c
/* 所有API的返回值类型 */
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
    /*无效的用户*/
    TTOS_INVALID_USER= 9,
    /*在中断中处理程序中执行*/
    TTOS_CALLED_FROM_ISR= 10,
    /*无效的大小*/
    TTOS_INVALID_SIZE =11,
    /* 超时 */
    TTOS_TIMEOUT = 12,
    /* 内部错误*/
    TTOS_INTERNAL_ERROR = 13,  
    /*无效的对齐 */
    TTOS_INVALID_ALIGNED = 14,
    /* 数值无效 */
    TTOS_INVALID_NUMBER = 15,  
    /* 消息太多 */
    TTOS_TOO_MANY = 16, 
    /* 对象已经被删除了 */
    TTOS_OBJECT_WAS_DELETED = 17,
    /* 无效的属性 */
    TTOS_INVALID_ATTRIBUTE = 18,
    /*无效的优先级 */
    TTOS_INVALID_PRIORITY = 19,
    /* 非资源拥有者 */
    TTOS_NOT_OWNER_OF_RESOURCE = 20,
    /* 资源正在被使用中 */
    TTOS_RESOURCE_IN_USE = 21,
    /* 对象版本不匹配 */
    TTOS_INVAILD_VERSION = 22,
    /*操作被屏蔽*/
    TTOS_MASKED = 23,    
    /*无效的索引*/
    TTOS_INVALID_INDEX = 24,    
    /*无效的系统调用*/
    TTOS_INVALID_SYSCALL = 25,    
    /*不支持互斥信号量嵌套获取*/
    TTOS_MUTEX_NESTING_NOT_ALLOWED = 26,  
    /*尝试获取互斥信号量的任务优先级高于当前互斥信号量天花板优先级*/
    TTOS_MUTEX_CEILING_VIOLATED = 27,  
    /*互斥信号量嵌套层数超出最大允许值*/
    TTOS_MUTEX_NEST_OVERFLOW = 28,  
    /* 对象已经被取消了 */
    TTOS_OBJECT_WAS_CANCELED = 29,
    /* 等待对象被信号中断 */
    TTOS_SIGNAL_INTR = 30,  
}T_TTOS_ReturnCode;

```

### 大括号风格

使用 Allman 风格，所有大括号另起一行：

```c
if (condition)
{
    do_something();
}
```

### 文件头格式

```c
/************************************************************************************************
 *              科东(广州)软件科技有限公司 版权所有
 *   Copyright (C) 2021 Intewell (Guangzhou) Software Technology Co., Ltd. All Rights Reserved.
 *************************************************************************************************/

/*
 * 修改历史：
 * 20xx-xx-xx     岳泽宇，科东(广州)软件科技有限公司
 *               创建该文件。
 */

/*
 * @file:  ttosVaddrWorkspace.c
 * @brief:
 *       <li>ttos逻辑地址空间管理。 </li>
 */

/*
 * @brief:
 *       <li>该模块提供ttos逻辑地址空间管理，提供逻辑地址空间分配相关接口 。</li>
 */

/************************头 文 件******************************/
#include <stdlib.h>
/************************宏 定 义******************************/
/************************类型定义******************************/
/************************外部声明******************************/
/************************前向声明******************************/
/************************模块变量******************************/
/************************全局变量******************************/
/************************实   现*******************************/
```
前向声明区只列出确实需要提前声明的函数，调用顺序已在定义之后的无需声明。

### 函数注释

```c
/**
 * @brief
 *
 * @param[in]  name  description
 * @param[out] name  description
 * @param[in,out] name  description
 * @return description
 */
```

## 核心约定

- 分配的最小对齐为 4K（页对齐）
- 使用 POSIX 及 pthread 接口，支持多任务并发
- 错误处理：失败时返回 `NULL`，库代码禁止调用 `abort`/`exit`
- 使用模块内全局 `arena_t` 变量管理虚拟地址空间，API 不通过参数传递句柄
- 兼容 32 位和 64 位的 RTOS
- 实现逻辑中优先考虑确定性的实时性和多任务场景
- TTOS_AllocVaddr函数需要带两个参数，size 和 offset，分别表示分配大小和分配出来的地址对齐

## 文档约束

- 生成的文档放在 `docs/` 目录下
- 将设计原理记录在 md 文件中，图文并茂，初级 RTOS 工程师也能看明白

## 测试

- 测试使用 `tests/` 中的轻量 C 测试框架编写
- 每个测试文件对应一个源码模块
- 须覆盖多任务并发场景，使用 pthread 模拟
- 提交前须通过 Valgrind / ASan 检查，不得有内存错误
- 增加接口响应时间测试，目的是确定实时性

## 工作流

- 修改代码后执行 `git commit`，message 使用中文书写
