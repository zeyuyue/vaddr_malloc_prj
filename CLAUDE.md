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
- 内部模块函数：`snake_case`，例如 `seg_node_alloc`
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

`TTOS_` 前缀的对外 API 统一使用 `T_TTOS_ReturnCode` 枚举作为返回值类型。
枚举定义位于 `include/ttosVaddrAlloc.h`，此处不再重复，以头文件为唯一真值来源。
常用返回码：`TTOS_OK`(0)、`TTOS_FAIL`(1)、`TTOS_INVALID_ADDRESS`(4)、`TTOS_INVALID_STATE`(6)、`TTOS_UNSATISFIED`(8)、`TTOS_INVALID_SIZE`(11)、`TTOS_INTERNAL_ERROR`(13)、`TTOS_INVALID_ALIGNED`(14)。

### 变量声明位置

函数内所有局部变量**必须**在函数体最顶部集中声明，在任何可执行语句之前。`for` 循环也不得在 `for (type var = ...)` 中声明变量，须提前在函数顶部声明。

```c
/* 正确 */
void foo(void)
{
    int    x;
    size_t i;

    x = bar();
    for (i = 0; i < 10; i++) { ... }
}

/* 错误：变量在语句中间声明 */
void foo(void)
{
    int x = bar();
    /* ... 其他语句 ... */
    for (size_t i = 0; i < 10; i++) { ... }  /* 禁止 */
}
```

### 大括号风格

- 使用 Allman 风格，所有大括号另起一行
- `if`、`for`、`while` 语句不得省略大括号，即使只有一条语句

```c
if (condition)
{
    do_something();
}
```

### 注释风格

注释分三类，各自有固定格式：

**① 函数文档注释**：使用 Doxygen `/** */` 块，内部行以 ` * ` 开头：

```c
/**
 * @brief 简要说明
 *
 * @param[in]     name  描述
 * @param[out]    name  描述
 * @param[in,out] name  描述
 * @return 描述
 */
```

**② 函数体内的行内注释**：每行注释独立用 `/* */` 包裹，**禁止**使用跨行的 `/* \n * \n */` 块格式，也禁止 `//`：

```c
/* 正确：每行独立一个 /* */ */
/* 说明第一点 */
/* 说明第二点 */

/* 错误示例（禁止在函数体内用跨行块）：
 * 说明第一点
 * 说明第二点
 */
```

**③ 文件头及节区说明注释**（修改历史、`@file`、`@brief`、宏定义区块说明等）：沿用文件头格式示例中的 `/* \n * \n */` 块风格，参见下方"文件头格式"。

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
 *       <li>ttos逻辑地址空间管理。</li>
 */

/*
 * @brief:
 *       <li>该模块提供ttos逻辑地址空间管理，提供逻辑地址空间分配相关接口。</li>
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

- 前向声明区只列出确实需要提前声明的函数，调用顺序已在定义之后的无需声明

## 核心约定

- 分配的最小对齐为 4K（页对齐），`TTOS_PAGE_SIZE` 和 `TTOS_PAGE_SHIFT` 在公共头文件 `ttosVaddrAlloc.h` 中定义，内部使用 `PAGE_SIZE`/`PAGE_SHIFT` 宏别名
- 使用 POSIX 及 pthread 接口，支持多任务并发
- 错误处理：失败时返回 `NULL`，库代码禁止调用 `abort`/`exit`
- 使用模块内全局 `arena_t` 变量管理虚拟地址空间，API 不通过参数传递句柄
- 兼容 32 位和 64 位的 RTOS
- 实现逻辑中优先考虑确定性的实时性和多任务场景
- `TTOS_AllocVaddr` 接口签名固定为 `void *TTOS_AllocVaddr(size_t size, size_t align)`，返回分配到的虚拟地址（失败返回 `NULL`），禁止修改为返回错误码或增减参数
- `TTOS_AllocVaddr` 的 `align` 参数须为 `PAGE_SIZE`（4096）的 2 的幂次倍，或传 `0` 表示默认页对齐；不满足时返回 `NULL`
- `TTOS_FreeVaddr` 接口签名固定为 `T_TTOS_ReturnCode TTOS_FreeVaddr(void *addr, size_t size)`，调用方须传入与分配时完全一致的 `addr` 和 `size`，禁止修改为 `void` 返回或增减参数

## 工作流

- 每次修改后不要自动调用测试用例去验证
- 每次修改代码后不要自动 `git commit` 提交
- 提交 `git commit` 时，message 使用中文书写

## 文档约束

- 生成的文档放在 `docs/` 目录下
- 将设计原理记录在 md 文件中，图文并茂，初级 RTOS 工程师也能看明白

## 测试

- 测试使用 `tests/` 中的轻量 C 测试框架编写
- 每个测试文件对应一个源码模块
- 须覆盖多任务并发场景，使用 pthread 模拟
- 提交前须通过 Valgrind / ASan 检查，不得有内存错误
- 增加接口响应时间测试，目的是确定实时性
