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
 * @file:  ttosTestFramework.h
 * @brief:
 *       <li>轻量级测试框架，提供断言宏和测试结果统计。</li>
 */

/*
 * @brief:
 *       <li>本头文件仅供 tests/ 目录下的测试文件使用，提供 TEST_ASSERT、
 *           TEST_SUITE_BEGIN、TEST_SUITE_RESULTS 三个宏，不依赖任何外部库。</li>
 */

#ifndef TTOS_TEST_FRAMEWORK_H
#define TTOS_TEST_FRAMEWORK_H

#include <stdio.h>

static int _g_pass = 0;
static int _g_fail = 0;

#define TEST_ASSERT(cond) \
    do { \
        if (cond) \
        { \
            _g_pass++; \
        } \
        else \
        { \
            _g_fail++; \
            fprintf(stderr, "FAIL  %s:%d  (%s)\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#define TEST_SUITE_BEGIN(name) \
    printf("--- %s ---\n", name)

#define TEST_SUITE_RESULTS() \
    do { \
        printf("Results: %d passed, %d failed\n", _g_pass, _g_fail); \
        return (_g_fail > 0) ? 1 : 0; \
    } while (0)

#endif /* TTOS_TEST_FRAMEWORK_H */
