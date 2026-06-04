/*
 * test_checkpoint.c - unit tests for the silent-checkpoint body decode.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_checkpoint.h"
#include "revice_test.h"

#include <stdint.h>

static void test_legacy_body_no_silent(void)
{
    /* 8-byte and 9-byte (with memspace) bodies have no silent byte. */
    uint8_t body8[8]  = {0};
    uint8_t body9[9]  = {0};
    CHECK_EQ_INT(checkpoint_decode_silent(body8, sizeof(body8)), 0);
    CHECK_EQ_INT(checkpoint_decode_silent(body9, sizeof(body9)), 0);
}

static void test_extended_body_silent_set(void)
{
    uint8_t body10[10] = {0};
    body10[9] = 1;
    CHECK_EQ_INT(checkpoint_decode_silent(body10, sizeof(body10)), 1);
    /* any non-zero value -> 1 */
    body10[9] = 0x42;
    CHECK_EQ_INT(checkpoint_decode_silent(body10, sizeof(body10)), 1);
}

static void test_extended_body_silent_clear(void)
{
    uint8_t body10[10] = {0};
    body10[9] = 0;
    CHECK_EQ_INT(checkpoint_decode_silent(body10, sizeof(body10)), 0);
}

static void test_null(void)
{
    CHECK_EQ_INT(checkpoint_decode_silent(NULL, 10), 0);
}

int main(void)
{
    test_legacy_body_no_silent();
    test_extended_body_silent_set();
    test_extended_body_silent_clear();
    test_null();
    REVICE_TEST_MAIN_END();
}
