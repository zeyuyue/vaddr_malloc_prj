# vaddr_malloc_prj 设计文档

> 面向初级 RTOS 工程师的完整说明，覆盖背景、数据结构、算法流程及实时性设计。

---

## 目录

1. [项目背景](#1-项目背景)
2. [整体架构](#2-整体架构)
3. [核心数据结构](#3-核心数据结构)
4. [两级分配策略](#4-两级分配策略)
5. [分配流程](#5-分配流程)
6. [释放与合并流程](#6-释放与合并流程)
7. [实时性设计：节点池](#7-实时性设计节点池)
8. [线程安全](#8-线程安全)
9. [32/64 位兼容](#932-64-位兼容)
10. [公共 API 速查](#10-公共-api-速查)
11. [文件说明](#11-文件说明)

---

## 1. 项目背景

在 RTOS 中，内核需要为驱动、任务栈、DMA 缓冲区等分配**连续的虚拟地址范围**。这与物理内存分配是两件独立的事：

```
调用者请求虚拟地址
        │
        ▼
  vaddr_malloc_prj          ← 本项目：只管"哪段虚拟地址被谁用"
  返回虚拟地址范围
        │
        ▼
  RTOS 内核建立页表          ← 内核的事：把虚拟地址映射到物理页
```

本项目**只负责虚拟地址的分配与释放**，不涉及物理内存、页表操作。

---

## 2. 整体架构

```
+----------------------------------------------------------+
|                    调用者（驱动 / 内核）                   |
|   TTOS_VaddrArenaInit()  TTOS_AllocVaddr()              |
|   TTOS_FreeVaddr()       TTOS_VaddrArenaStats()          |
+---------------------------┬------------------------------+
                            │ 公共 API（ttosVaddrAlloc.h）
+---------------------------▼------------------------------+
|                     全局 arena 实例                       |
|   g_vaddr_arena                                          |
|                                                          |
|   ┌─────────────┐     ┌──────────────────────────────┐  |
|   │   bitmap    │     │       seg_list（链表）        │  |
|   │ 1 bit/page  │     │  free_seg → free_seg → ...   │  |
|   └─────────────┘     └──────────────────────────────┘  |
|                                                          |
|   ┌──────────────────────────────────────────────────┐  |
|   │            node_pool（预分配节点池）              │  |
|   │  [ ][ ][ ][ ][ ][ ] ... （总数 = pages/2 + 2）   │  |
|   └──────────────────────────────────────────────────┘  |
+----------------------------------------------------------+
     ttosVaddrArena.c            ttosVaddrAlloc.c
```

---

## 3. 核心数据结构

### 3.1 `arena_t` — 管理控制块

```
arena_t
┌─────────────────┬──────────────────────────────────────┐
│ base            │ 虚拟地址起始，如 0x10000000           │
│ total_pages     │ 总页数，如 256（= 1 MB / 4 KB）      │
│ free_pages      │ 当前空闲页数（快速 OOM 检查）        │
│ bitmap          │ → [ 位图数组 ]                        │
│ bitmap_bytes    │ 位图字节数 = ceil(total_pages / 8)   │
│ seg_list        │ → 空闲段链表头                        │
│ node_pool       │ → 节点池数组                          │
│ node_free_list  │ → 空闲节点链表头（LIFO 栈）          │
│ node_pool_size  │ 节点池容量 = total_pages/2 + 2       │
│ lock            │ pthread_mutex_t                       │
└─────────────────┴──────────────────────────────────────┘
```

### 3.2 `free_seg_t` — 空闲段节点

每个节点代表一段**连续空闲页**：

```
free_seg_t
┌────────────────┬──────────────────────────┐
│ start_page     │ 空闲段起始页索引          │
│ page_count     │ 连续空闲页数              │
│ prev           │ 前驱节点指针              │
│ next           │ 后继节点指针              │
└────────────────┴──────────────────────────┘
```

链表按 `start_page` **升序**排列：

```
seg_list
   │
   ▼
[page=0, count=4] ←→ [page=10, count=2] ←→ [page=20, count=8]
```

### 3.3 位图（bitmap）

位图是页状态的唯一真值来源，每页占 1 bit：

```
页索引:   0  1  2  3  4  5  6  7  8  9 ...
bit  :    1  1  0  0  1  0  0  0  1  1 ...
          ↑已分配     ↑空闲           ↑已分配
```

读写操作：

```c
/* 第 page 页所在字节 = page / 8，所在位 = page % 8 */
bitmap[page >> 3] |=  (1u << (page & 7u));  /* 标记已分配 */
bitmap[page >> 3] &= ~(1u << (page & 7u));  /* 标记空闲   */
```

---

## 4. 分配策略：段链表首次适配

所有大小的请求统一通过空闲段链表的首次适配完成分配。位图**不参与分配路径搜索**，仅作为页状态的权威记录，用于双重释放检测和状态追踪。

```
请求 size
    │
    ▼
size_to_pages(size)  →  n_pages
    │
    ▼
seg_find_fit(seg_list, n_pages)
遍历有序空闲段链表，找第一个 page_count ≥ n 的节点
    │
    ▼
seg_carve_front()  从节点头部截取 n 页
```

| 操作 | 搜索结构 | 时间复杂度 |
|------|----------|-----------|
| 分配搜索 | seg_list 线性遍历 | O(空闲段数) |
| 位图标记 | bitmap 逐位设置 | O(n_pages) |
| 节点取用/归还 | node_free_list | O(1) |

> **为什么用段链表而不是位图扫描？**
> 段链表节点本身就代表连续空闲区域，无需逐位扫描即可判定是否满足请求。
> 位图保留为页级状态记录，提供 O(1) 的单页查询能力，用于释放时的双重释放检测。

---

## 5. 分配流程

```
TTOS_AllocVaddr(size)
        │
        ▼
  参数校验（bitmap 非空、size > 0）
        │
        ▼
  size_to_pages(size)  →  n_pages
  （无溢出写法：((size-1) >> PAGE_SHIFT) + 1）
        │
        ▼
  加锁（pthread_mutex_lock）
        │
        ▼
  快速 OOM 检查：n_pages > free_pages？ ──是──→ 解锁，返回 NULL
        │否
        ▼
  seg_find_fit(seg_list, n_pages)
  遍历链表，找第一个 page_count ≥ n 的节点
        │
  找不到？ ──→ 解锁，返回 NULL
        │
        ▼
  seg_carve_front(arena, seg, n_pages)  从节点头部截取：
  ┌──────────────────────────────────────┐
  │  [  分配区  ][      剩余空闲        ]│
  │  ↑截走         ↑节点缩减就地保留    │
  └──────────────────────────────────────┘
  若节点被完全消耗，从链表移除并归还节点池
        │
        ▼
  bitmap_mark_used()  位图对应位置 1
  free_pages -= n_pages
        │
        ▼
  解锁，返回虚拟地址 = base + start_page × 4096
```

---

## 6. 释放与合并流程

```
TTOS_FreeVaddr(addr, size)
        │
        ▼
  参数校验（非空、地址对齐、范围检查）
        │
        ▼
  加锁
        │
        ▼
  双重释放检测：遍历范围内所有页的位图位，
  若任意一页已为空闲（bit=0）则拒绝，解锁返回
        │
        ▼
  seg_node_alloc()  先从节点池取一个节点
  （节点池耗尽时放弃释放，保证状态一致性）
        │
        ▼
  bitmap_mark_free()  对应位清零
  free_pages += n_pages
        │
        ▼
  seg_insert()  按地址升序插入链表
        │
        ▼
  seg_merge_adjacent()  合并相邻空闲段：

  插入前：  [A: 0~3] [B: 8~11]
  插入节点：         [C: 4~7]
  合并后：  [A+C: 0~7]        （C 与 A 连续，合并到 A）
  再合并：  [A+C+B: 0~11]     （合并结果与 B 连续，再合并）

        │
        ▼
  解锁
```

关键设计：**先取节点，再更新状态**。若 `seg_node_alloc` 失败（节点池耗尽），
位图和 `free_pages` 均未修改，arena 状态保持一致。

合并规则：

```
若 node.start + node.count == node.next.start  →  与后继合并
若 node.prev.start + node.prev.count == node.start  →  与前驱合并（node 被消耗）
```

合并后，相邻节点归还节点池，链表始终保持最大连续段，减少碎片。

---

## 7. 实时性设计：节点池

### 7.1 问题

如果在分配/释放路径中调用 `calloc`/`free`，堆分配器的执行时间**不确定**，在 RTOS 中可能导致任务错过截止期。

### 7.2 解决方案：预分配节点池

```
初始化时（只调用一次 calloc）：

node_pool: [ ][ ][ ][ ][ ][ ][ ][ ] ...  共 total_pages/2 + 2 个节点
             │  │  │  │
             └──┴──┴──┘ 串成空闲链表
                        node_free_list → [0] → [1] → [2] → ...
```

**取节点（O(1)，无系统调用）：**

```
node_free_list → [head] → [next] → ...

取出 head，node_free_list 移到 next
```

**还节点（O(1)，无系统调用）：**

```
node_free_list → [旧head] → ...

将 node 插到链表头，node_free_list 指向 node
```

### 7.3 节点池容量计算

```
最坏情况（每隔一页分配一页）：

页:   [A][F][A][F][A][F][A][F]...
         ↑   ↑   ↑   ↑
      每个 F 是独立的空闲段 → 最多 total_pages/2 个节点

+2 保留：初始整段节点（1个）+ 合并过程中的临时余量（1个）

节点池容量 = total_pages / 2 + 2
```

### 7.4 复杂度说明

节点池解决了堆分配器的非确定性问题，但**段链表的遍历仍为线性**：

| 操作 | 复杂度 | 说明 |
|------|--------|------|
| 节点取用/归还 | **O(1)** | 链表头部插入/弹出，无系统调用 |
| `seg_find_fit` 首次适配 | O(空闲段数) | 最坏 O(total_pages/2) |
| `seg_insert` 有序插入 | O(空闲段数) | 最坏 O(total_pages/2) |
| `bitmap_mark_used/free` | O(n_pages) | 与请求页数成正比 |

对于碎片化严重的场景（如交替页分配），持锁期间的遍历时间与总页数成正比。
若需严格 O(1) 或 O(log n) 有界的分配时间，可考虑升级为分级位图（buddy）、
跳表或分离空闲链表等数据结构。

---

## 8. 线程安全

所有公共 API 使用同一把 `pthread_mutex_t` 保护整个 arena：

```
线程1: TTOS_AllocVaddr()     线程2: TTOS_FreeVaddr()
          │                              │
          ▼                              ▼
     pthread_mutex_lock()          等待锁...
          │                              │
     操作 bitmap + seg_list         获得锁后操作
          │                              │
     pthread_mutex_unlock()        pthread_mutex_unlock()
```

持锁期间**不调用任何堆分配器**（节点池保证），避免了堆分配器引入的不确定延迟。
锁持有时间与空闲段数成正比（见 7.4 节），在碎片化可控的场景下适合多任务 RTOS 环境。

---

## 9. 32/64 位兼容

| 类型 | 用途 | 32位 | 64位 |
|------|------|------|------|
| `uintptr_t` | 虚拟地址 | 4 字节 | 8 字节 |
| `size_t` | 长度、页数、索引 | 4 字节 | 8 字节 |
| `uint8_t` | 位图元素 | 1 字节 | 1 字节 |

代码中无任何硬编码地址宽度假设，直接编译即可在 32 位和 64 位 RTOS 上使用。

---

## 10. 公共 API 速查

```c
/* 初始化全局 arena，管理 [base, base+size) 的虚拟地址区间
 * base > 0 且页对齐，base+size 不得溢出 */
int  TTOS_VaddrArenaInit(uintptr_t base, size_t size);

/* 销毁 arena，释放所有内部元数据 */
void TTOS_VaddrArenaDestroy(void);

/* 分配至少 size 字节的连续虚拟地址，返回页对齐地址 */
void *TTOS_AllocVaddr(size_t size);

/* 释放 TTOS_AllocVaddr 分配的地址，addr 和 size 须与分配时一致 */
void TTOS_FreeVaddr(void *addr, size_t size);

/* 查询统计信息（总页数、空闲页数、已用页数） */
int  TTOS_VaddrArenaStats(TTOS_VaddrStats *stats);
```

典型使用流程：

```c
/* 1. 初始化：将 0x10000000 起的 1 MB 虚拟地址交给 arena 管理 */
TTOS_VaddrArenaInit(0x10000000UL, 1 * 1024 * 1024);

/* 2. 分配 64 KB 连续虚拟地址 */
void *vaddr = TTOS_AllocVaddr(64 * 1024);

/* 3. 使用该地址（由 RTOS 内核建立页表后可访问） */

/* 4. 释放 */
TTOS_FreeVaddr(vaddr, 64 * 1024);

/* 5. 销毁 */
TTOS_VaddrArenaDestroy();
```

---

## 11. 文件说明

| 文件 | 说明 |
|------|------|
| `include/ttosVaddrAlloc.h` | 公共 API 声明，调用者只需包含此头文件 |
| `include/ttosVaddrInternal.h` | 内部类型定义（`arena_t`、`free_seg_t`、位图操作），仅 src/ 使用 |
| `src/ttosVaddrArena.c` | arena 生命周期、节点池、段链表操作 |
| `src/ttosVaddrAlloc.c` | `TTOS_AllocVaddr` / `TTOS_FreeVaddr` 实现 |
| `tests/ttosTestBasic.c` | 基础功能测试（初始化、分配、释放、OOM、碎片合并） |
| `tests/ttosTestConcurrent.c` | 并发测试（多线程交替操作、地址无重叠验证） |
| `tests/ttosTestFramework.h` | 轻量测试宏 |
| `docs/design.md` | 本文档 |
