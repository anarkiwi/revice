/*
 * revice_test.h - tiny assert-based test harness shared by the revice core
 *                 unit tests. No external framework: a test executable is a
 *                 plain main() that runs CHECK*() macros and ends with
 *                 REVICE_TEST_MAIN_END(), returning non-zero on any failure.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#ifndef REVICE_TEST_H
#define REVICE_TEST_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int revice_tests_run = 0;
static int revice_tests_failed = 0;

static inline void revice_hexdump(FILE *f, const char *pfx,
                                  const unsigned char *p, size_t n)
{
    size_t i;
    fprintf(f, "%s", pfx);
    for (i = 0; i < n; i++) {
        fprintf(f, " %02x", p[i]);
    }
    fprintf(f, "\n");
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        revice_tests_run++;                                                  \
        if (!(cond)) {                                                       \
            revice_tests_failed++;                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                   \
    do {                                                                     \
        long _a = (long)(a), _b = (long)(b);                                 \
        revice_tests_run++;                                                  \
        if (_a != _b) {                                                      \
            revice_tests_failed++;                                           \
            fprintf(stderr, "FAIL %s:%d: %s == %s (%ld != %ld)\n",           \
                    __FILE__, __LINE__, #a, #b, _a, _b);                     \
        }                                                                    \
    } while (0)

#define CHECK_MEM(a, b, n)                                                   \
    do {                                                                     \
        revice_tests_run++;                                                  \
        if (memcmp((a), (b), (n)) != 0) {                                    \
            revice_tests_failed++;                                           \
            fprintf(stderr, "FAIL %s:%d: mem %s != %s (%zu bytes)\n",        \
                    __FILE__, __LINE__, #a, #b, (size_t)(n));                \
            revice_hexdump(stderr, "  got", (const unsigned char *)(a), (n));\
            revice_hexdump(stderr, "  exp", (const unsigned char *)(b), (n));\
        }                                                                    \
    } while (0)

#define REVICE_TEST_MAIN_END()                                               \
    do {                                                                     \
        fprintf(stderr, "%s: %d checks, %d failed\n",                        \
                __FILE__, revice_tests_run, revice_tests_failed);            \
        return revice_tests_failed ? 1 : 0;                                  \
    } while (0)

#endif /* REVICE_TEST_H */
