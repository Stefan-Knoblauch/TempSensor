/*
 * test.h - Minimal test framework (no external libraries).
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>

extern int g_checks;
extern int g_fails;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_fails++;                                                       \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        long va_ = (long)(a), vb_ = (long)(b);                               \
        g_checks++;                                                          \
        if (va_ != vb_) {                                                    \
            g_fails++;                                                       \
            printf("    FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__,       \
                   __LINE__, #a, #b, va_, vb_);                              \
        }                                                                    \
    } while (0)

typedef void (*test_fn)(void);
typedef struct {
    const char *name;
    test_fn fn;
} test_case_t;

/* Each test file exports a NULL-terminated table. */
extern const test_case_t payload_tests[];
extern const test_case_t rtsm_tests[];

#endif /* TEST_H */
