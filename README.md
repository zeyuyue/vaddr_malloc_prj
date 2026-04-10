# vaddr_malloc_prj

> 一个用 Claude Code 辅助开发的 RTOS 虚拟地址空间分配器练手项目。

本项目实现了一个面向 RTOS 场景的虚拟地址空间分配器，支持页对齐分配、自定义对齐约束、并发安全释放及相邻空闲段自动合并。

---

## 目录

- [项目背景](#项目背景)
- [整体架构](#整体架构)
- [核心数据结构](#核心数据结构)
- [分配原理](#分配原理)
- [释放与合并原理](#释放与合并原理)
- [实时性设计](#实时性设计)
- [关键设计取舍](#关键设计取舍)
- [并发安全](#并发安全)
- [公共 API](#公共-api)
- [目录结构](#目录结构)
- [构建与测试](#构建与测试)

---

## 项目背景

在 RTOS 中，内核需要管理一段连续的虚拟地址区间（Arena），按需分配给驱动或任务使用。与物理内存分配不同，虚拟地址分配器只管理**地址范围**，不涉及实际物理页映射。

本项目目标：
- 支持任意大小（向上取整到页）的连续虚拟地址分配
- 支持自定义对齐约束（PAGE_SIZE 的 2 的幂次倍）
- 并发安全，适合多任务 RTOS 环境
- 关键路径不调用堆分配器，满足实时性要求
- 检测双重释放，防止状态损坏

---

## 整体架构

```
调用方（内核 / 驱动）
        │
        │  TTOS_AllocVaddr(size, align)
        │  TTOS_FreeVaddr(addr, size)
        ▼
┌──────────────────────────────────────────────┐
│               公共接口层                      │
│          ttosVaddrAlloc.c                    │
│  • 参数校验  • 对齐计算  • 首次适配搜索       │
└──────────────┬───────────────────────────────┘
               │  调用
               ▼
┌──────────────────────────────────────────────┐
│              Arena 管理层                     │
│          ttosVaddrArena.c                    │
│  • 初始化/销毁  • 段链表插入/删除/合并        │
│  • 节点池管理  • 统计查询                     │
└──────┬───────────────────┬────────────────────┘
       │                   │
       ▼                   ▼
┌─────────────┐   ┌─────────────────────────┐
│   位图       │   │     空闲段链表           │
│  (bitmap)   │   │     (seg_list)          │
│             │   │                         │
│  每页 1 bit  │   │  [seg0]←→[seg1]←→[seg2] │
│  1 = 已分配  │   │  按 start_page 升序排列  │
│  0 = 空闲   │   │                         │
└─────────────┘   └─────────────────────────┘
       │
       └── 双重释放检测的唯一真值来源
```

---

## 核心数据结构

### Arena（地址空间管理器）

```
arena_t
┌────────────────────────────────────────────────┐
│ base          虚拟地址起始值（如 0x10000000）    │
│ total_pages   总页数                            │
│ free_pages    当前空闲页数（快速 OOM 判断）      │
│ bitmap ──────────────────────────────────────► │
│               [ 0 1 1 0 0 1 0 0 ... ]          │
│               每个 bit 对应一页，1=已分配        │
│ seg_list ────────────────────────────────────► │
│               [start=0, cnt=4] ←→ [start=6, cnt=2] ←→ ... │
│               按起始页升序的空闲段双向链表        │
│ node_pool ───────────────────────────────────► │
│               预分配的 free_seg_t 节点数组       │
│ node_free_list  空闲节点 LIFO 栈头              │
│ lock          pthread_mutex_t 互斥锁            │
└────────────────────────────────────────────────┘
```

### 空闲段节点（free_seg_t）

```
free_seg_t
┌─────────────────────────────────────────────┐
│ start_page   该空闲段的起始页索引            │
│ page_count   连续空闲页数                   │
│ prev ──────► 前一个空闲段（地址更小）        │
│ next ──────► 后一个空闲段（地址更大）        │
└─────────────────────────────────────────────┘
```

### 节点池设计

Arena 初始化时一次性分配 `total_pages / 2 + 2` 个节点：

```
worst case（最大碎片化）：

页:  [0][1][2][3][4][5][6][7]...
状态: 用 闲 用 闲 用 闲 用 闲

空闲段数 = total_pages / 2  ← 节点需求最大值
```

额外 +2 保留初始整段节点及合并过程中的临时余量，确保任何时刻节点池都不会耗尽。

---

## 分配原理

### 无对齐约束（align = 0）

使用**首次适配**（First Fit）算法遍历空闲段链表，找到第一个能容纳 `n` 页的段：

```
请求: 3 页

seg_list: [start=0, cnt=2] → [start=4, cnt=6] → [start=12, cnt=1]

第 1 段：2 页 < 3，跳过
第 2 段：6 页 ≥ 3，命中！

分配页 4、5、6，将段拆分为：
  前余量: [start=4, cnt=0]  → 不存在，丢弃
  后余量: [start=7, cnt=3]  → 插回链表

结果 seg_list: [start=0, cnt=2] → [start=7, cnt=3] → [start=12, cnt=1]
```

### 有对齐约束（align > PAGE_SIZE）

在每个候选段内计算满足对齐的第一个起始页，再判断剩余容量是否足够：

```
请求: 2 页，align = 4 * PAGE_SIZE（即 4 页对齐）

arena.base = 0x10000000（= 页索引 0x10000）

候选段: [start_page=1, cnt=8]

绝对页索引 = 0x10000 + 1 = 0x10001
0x10001 % 4 = 1（余 1，需补 3 页才能对齐）

对齐起始页 = 1 + (4 - 1) = 4
剩余容量   = 1 + 8 - 4 = 5 ≥ 2，命中！

分配页 4、5，拆分为：
  前余量: [start=1, cnt=3]
  后余量: [start=6, cnt=3]
```

### 分配流程图

```
TTOS_AllocVaddr(size, align)
        │
        ├─ size == 0 或 arena 未初始化？→ return NULL
        ├─ align 不合法？→ return NULL
        │
        ▼
   加锁 (mutex_lock)
        │
        ├─ free_pages < n_pages？→ 解锁，return NULL（快速 OOM）
        │
        ▼
   seg_find_fit_aligned()  ← 首次适配，找到满足对齐的空闲段
        │
        ├─ 未找到？→ 解锁，return NULL
        │
        ▼
   seg_carve_aligned()     ← 从段中截取 n 页，前后余量插回链表
        │
   bitmap_mark_used()      ← 标记对应位为 1
   free_pages -= n_pages
        │
   解锁 (mutex_unlock)
        │
        └─ return (void*)(base + start_page * PAGE_SIZE)
```

---

## 释放与合并原理

### 释放流程

```
TTOS_FreeVaddr(addr, size)
        │
        ├─ 参数校验（arena 状态、地址对齐、范围合法性）
        │
        ▼
   加锁
        │
        ├─ 扫描位图 [start, start+n)，任意页为 0？→ 双重释放！解锁返回 FAIL
        │
        ▼
   seg_node_alloc()        ← 从节点池取一个节点（O(1)）
        │
        ├─ 节点池耗尽？→ 解锁，返回 TTOS_UNSATISFIED（状态未被修改）
        │
        ▼
   bitmap_mark_free()      ← 清零对应位
   free_pages += n_pages
   seg_insert()            ← 按地址序插入链表
   seg_merge_adjacent()    ← 尝试与前后相邻段合并
        │
   解锁
        └─ return TTOS_OK
```

### 相邻段合并

```
释放前:
  [used][used][FREE-A][used][used]
  seg_list: → [seg_A: start=2, cnt=1]

释放中间块后:
  [used][FREE-B][FREE-A][used][used]
  seg_list: → [seg_B: start=1, cnt=1] → [seg_A: start=2, cnt=1]

seg_merge_adjacent(seg_B):
  B.end (2) == A.start (2) → 合并后继
  seg_B.cnt = 1 + 1 = 2，回收 seg_A 节点

  seg_list: → [seg_B: start=1, cnt=2]

再释放左侧（假设 seg_C 在 start=0）:
  seg_C.end (1) == seg_B.start (1) → 合并前驱
  seg_C.cnt += 2，回收 seg_B 节点

  seg_list: → [seg_C: start=0, cnt=3]  ← 三段合一
```

---

## 实时性设计

| 操作 | 时间复杂度 | 说明 |
|------|-----------|------|
| 节点取用 / 归还 | O(1) | LIFO 栈，不调用堆分配器 |
| 快速 OOM 判断 | O(1) | 比较 `free_pages` 计数器 |
| 首次适配搜索 | O(空闲段数) | 最坏 O(total_pages/2) |
| 段插入（有序） | O(空闲段数) | 最坏 O(total_pages/2) |
| 相邻段合并 | O(1) | 只检查直接前驱/后继 |
| 双重释放检测 | O(n_pages) | 逐页扫描被释放范围 |

关键路径（持锁内）**不调用** `malloc`/`free`，节点在 `TTOS_VaddrArenaInit` 时一次性预分配。

---

## 关键设计取舍

### 不存储分配元数据（size 由调用方保证）

本分配器**不记录每次分配的大小**，`TTOS_FreeVaddr` 完全信任调用方传入的 `addr` 和 `size`，要求二者与分配时完全一致。

**为什么这样设计？**

存储元数据（如在每个分配块前插入 header 记录大小）会带来：
- 关键路径内额外的堆分配或固定表查询开销
- 与 RTOS 实时性目标（持锁路径 O(1)、不调用堆分配器）相冲突

去掉元数据后，节点取用/归还为严格 O(1)，关键路径内零动态内存分配，满足确定性实时性要求。

**size 传错时的后果**

| 错误类型 | 现象 | 能否被检测到 |
|---------|------|------------|
| size 偏小（少于实际分配页数） | 尾部页永久泄漏，无法再被分配 | 不能 |
| size 偏大，越界页恰好空闲 | 双重释放检测命中，返回 `TTOS_FAIL` | 能 |
| size 偏大，越界页被其他分配占用 | 其他分配的页被悄悄标记为空闲，后续可能被分配给第三方，造成两个调用方持有同一地址范围 | **不能，静默损坏** |

最危险的情况如下图所示：

```
分配A 占用: [页4][页5]         bitmap: 1 1 _ _
分配B 占用: [页6][页7]         bitmap: _ _ 1 1

调用方错误地以 4 页大小释放分配A:
  双重释放检测扫描 页4~页7，全为 1 → 通过检测！
  bitmap 全部清零: 0 0 0 0
  seg_list 插入 [start=4, cnt=4]

  → 分配B 的页6、页7 被释放，但分配B 的持有者毫不知情
  → 下次分配可能将 页6/页7 分配给分配C
  → 分配B 与分配C 同时持有相同的虚拟地址范围 ← 严重的静默错误
```

**调用方责任**

这是一个有意识的取舍——将"记录分配大小"的责任转移给调用方，换取更低的运行时开销和更强的实时确定性。调用方通常是内核或驱动，有条件在自身的数据结构中维护这一信息。

若未来需要加强保护，可考虑引入分配记录表（地址→页数映射），但须权衡额外的内存占用与查询开销。

---

## 并发安全

- 所有对 `arena` 字段的读写均在 `pthread_mutex_t lock` 保护下进行
- `TTOS_AllocVaddr` 和 `TTOS_FreeVaddr` 均持锁操作，线程安全
- `TTOS_VaddrArenaDestroy` 在销毁前先持锁将 `bitmap` 指针置 NULL，确保其他线程在解锁后检查 `bitmap == NULL` 时能安全退出，避免访问已释放的 mutex

---

## 公共 API

```c
// 初始化 arena，管理 [base, base+size) 虚拟地址区间
T_TTOS_ReturnCode TTOS_VaddrArenaInit(uintptr_t base, size_t size);

// 销毁 arena，释放内部元数据
void TTOS_VaddrArenaDestroy(void);

// 分配至少 size 字节的连续虚拟地址，align 为对齐约束（0 = 默认页对齐）
void *TTOS_AllocVaddr(size_t size, size_t align);

// 释放由 TTOS_AllocVaddr 分配的地址范围，addr/size 须与分配时一致
T_TTOS_ReturnCode TTOS_FreeVaddr(void *addr, size_t size);

// 查询当前 arena 统计信息（总页数、空闲页数、已用页数）
T_TTOS_ReturnCode TTOS_VaddrArenaStats(TTOS_VaddrStats *stats);
```

### 返回码速查

| 返回码 | 值 | 含义 |
|--------|---|------|
| `TTOS_OK` | 0 | 成功 |
| `TTOS_FAIL` | 1 | 操作失败（如双重释放） |
| `TTOS_INVALID_ADDRESS` | 4 | 地址非法或为 NULL |
| `TTOS_INVALID_STATE` | 6 | Arena 未初始化 |
| `TTOS_UNSATISFIED` | 8 | 资源不足（OOM 或节点池耗尽） |
| `TTOS_INVALID_SIZE` | 11 | size 为 0 |
| `TTOS_INTERNAL_ERROR` | 13 | 内部初始化失败 |
| `TTOS_INVALID_ALIGNED` | 14 | base/size/align 未满足对齐要求 |

---

## 目录结构

```
vaddr_malloc_prj/
├── include/
│   ├── ttosVaddrAlloc.h       # 对外公共接口与返回码定义
│   └── ttosVaddrInternal.h    # 内部类型、位图操作（仅 src/ 使用）
├── src/
│   ├── ttosVaddrAlloc.c       # 分配/释放核心逻辑
│   └── ttosVaddrArena.c       # Arena 生命周期与段链表操作
├── tests/
│   ├── ttosTestFramework.h    # 轻量测试宏框架
│   ├── ttosTestBasic.c        # 基础功能测试
│   ├── ttosTestConcurrent.c   # 多线程并发测试
│   └── ttosTestLatency.c      # 接口响应时间测试
├── .claude/
│   └── code-style.md          # 代码风格规范
├── CLAUDE.md                  # Claude Code 项目指引
└── Makefile
```

---

## 构建与测试

```bash
make          # 构建静态库 build/libvaddr.a
make test     # 编译并运行全部测试
make asan     # 以 AddressSanitizer 运行测试（内存错误检测）
make clean    # 清理构建产物
```
