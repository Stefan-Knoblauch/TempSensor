/*
 * test_main.c - Runs every registered test; exit status 1 on any failure.
 */
#include "test.h"

int g_checks = 0;
int g_fails = 0;

static int run_table(const test_case_t *t, int *failed_tests)
{
    int n = 0;
    for (; t->name != NULL; t++) {
        int before = g_fails;
        t->fn();
        printf("[%s] %s\n", g_fails == before ? "PASS" : "FAIL", t->name);
        if (g_fails != before) {
            (*failed_tests)++;
        }
        n++;
    }
    return n;
}

int main(void)
{
    int failed = 0;
    int n = run_table(payload_tests, &failed);
    n += run_table(rtsm_tests, &failed);
    printf("\n%d tests, %d checks, %d failed tests, %d failed checks\n", n, g_checks, failed,
           g_fails);
    return g_fails == 0 ? 0 : 1;
}
