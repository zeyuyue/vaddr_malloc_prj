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

详见 [.claude/code-style.md](.claude/code-style.md)。

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

- 没有单独指示，不要去修改测试用例
- 每次修改后，不要自动调用测试用例去验证
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
