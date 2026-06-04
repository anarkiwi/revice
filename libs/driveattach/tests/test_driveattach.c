/*
 * test_driveattach.c - unit tests for the DRIVE_ATTACH body decoder.
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See LICENSE for copyright notice (GPL-2.0-or-later).
 */

#include "revice_driveattach.h"
#include "revice_test.h"

#include <stdint.h>
#include <string.h>

static void test_attach(void)
{
    /* unit 8, drive 0, path "x.d64" */
    uint8_t body[] = { 8, 0, 5, 'x', '.', 'd', '6', '4' };
    da_request_t req;
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_OK);
    CHECK_EQ_INT(req.detach, 0);
    CHECK_EQ_INT(req.unit, 8);
    CHECK_EQ_INT(req.drive, 0);
    CHECK(strcmp(req.path, "x.d64") == 0);
}

static void test_detach(void)
{
    uint8_t body[] = { 9, 1, 0 };       /* path_len 0 -> detach */
    da_request_t req;
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_OK);
    CHECK_EQ_INT(req.detach, 1);
    CHECK_EQ_INT(req.unit, 9);
    CHECK_EQ_INT(req.drive, 1);
    CHECK_EQ_INT(req.path[0], '\0');
}

static void test_short_body(void)
{
    uint8_t body[] = { 8, 0 };          /* < 3 bytes */
    da_request_t req;
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_ERR_LENGTH);
}

static void test_truncated_path(void)
{
    uint8_t body[] = { 8, 0, 5, 'x' };  /* claims 5 path bytes, only 1 present */
    da_request_t req;
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_ERR_LENGTH);
}

static void test_bad_unit(void)
{
    uint8_t body[] = { 7, 0, 0 };       /* unit < 8 */
    da_request_t req;
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_ERR_PARAM);
    body[0] = 12;                       /* unit > 11 */
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_ERR_PARAM);
}

static void test_bad_drive(void)
{
    uint8_t body[] = { 8, 2, 0 };       /* drive > 1 */
    da_request_t req;
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_ERR_PARAM);
}

static void test_max_path(void)
{
    uint8_t body[3 + 255];
    da_request_t req;
    int i;
    body[0] = 11;
    body[1] = 0;
    body[2] = 255;
    for (i = 0; i < 255; i++) {
        body[3 + i] = 'a';
    }
    CHECK_EQ_INT(da_decode(body, sizeof(body), &req), DA_OK);
    CHECK_EQ_INT((int)strlen(req.path), 255);
    CHECK_EQ_INT(req.path[255], '\0');
}

int main(void)
{
    test_attach();
    test_detach();
    test_short_body();
    test_truncated_path();
    test_bad_unit();
    test_bad_drive();
    test_max_path();
    REVICE_TEST_MAIN_END();
}
