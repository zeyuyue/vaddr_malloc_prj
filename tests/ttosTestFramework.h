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
