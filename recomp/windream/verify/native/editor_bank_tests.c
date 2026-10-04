/*
 * The project bank's codec (host/sdl/editor_bank_rle.c, spec 008 phase 4) on
 * a real DREAMS.DAT: argv[1] is the file. Built and run by
 * tests/recomp/test_editor_bank.py.
 *
 *   1. It unpacks into 150 records of 0x2200 bytes and packs back to the same
 *      bytes, byte for byte (the padding 0x25c..0x3ff kept from the file).
 *   2. An edit of one record changes only that record's bytes and the later
 *      offsets, and unpacks back to the edit.
 *   3. A pack that would pass the cap is refused; a file whose last record is
 *      cut, or whose offsets run backwards, does not unpack.
 * Prints one "ok ..." line per check, exits 1 at the first failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor_bank_rle.h"

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); return 1; } } while (0)

static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: editor_bank_tests DREAMS.DAT\n"); return 2; }
    FILE* f = fopen(argv[1], "rb");
    CHECK(f, "cannot open %s", argv[1]);
    static uint8_t file[BANK_FILE_CAP], packed[BANK_FILE_CAP];
    size_t size = fread(file, 1, sizeof file, f);
    fclose(f);
    uint8_t* records = (uint8_t*)malloc(BANK_RECORDS * BANK_RECORD_SIZE);
    uint8_t* again = (uint8_t*)malloc(BANK_RECORDS * BANK_RECORD_SIZE);
    char why[200];

    CHECK(bank_rle_unpack(file, size, records, why, sizeof why), "unpack: %s", why);
    memcpy(packed, file, BANK_HEADER);
    size_t n = bank_rle_pack(records, packed, sizeof packed);
    CHECK(n == size, "packed %u bytes, the file has %u", (unsigned)n, (unsigned)size);
    CHECK(!memcmp(packed, file, size), "the packed bank differs from the file");
    printf("ok round trip: %u bytes, %d records\n", (unsigned)size, BANK_RECORDS);

    /* Project12's +0x1e4 (sky turn rate) */
    uint8_t* sky = records + 12 * BANK_RECORD_SIZE + 0x1E4;
    sky[0] ^= 0x5A;
    memcpy(packed, file, BANK_HEADER);
    n = bank_rle_pack(records, packed, sizeof packed);
    CHECK(n, "the edited bank does not pack");
    for (int i = 0; i <= 12; i++)
        CHECK(get32(packed + 4 * i) == get32(file + 4 * i), "offset %d moved", i);
    uint32_t start = BANK_HEADER + get32(file + 4 * 12);
    CHECK(!memcmp(packed + BANK_HEADER, file + BANK_HEADER, start - BANK_HEADER), "records before 12 changed");
    int32_t shift = (int32_t)n - (int32_t)size;
    for (int i = 13; i <= BANK_RECORDS; i++)
        CHECK((int32_t)get32(packed + 4 * i) - (int32_t)get32(file + 4 * i) == shift, "offset %d", i);
    uint32_t after = BANK_HEADER + get32(file + 4 * 13);
    CHECK(!memcmp(packed + after + shift, file + after, size - after), "records after 12 changed");
    CHECK(bank_rle_unpack(packed, n, again, why, sizeof why), "unpack the edit: %s", why);
    CHECK(!memcmp(again, records, BANK_RECORDS * BANK_RECORD_SIZE), "the edit did not unpack back");
    printf("ok one edit: record 12 only, later offsets shift by %d\n", (int)shift);
    sky[0] ^= 0x5A;

    memcpy(packed, file, BANK_HEADER);
    CHECK(!bank_rle_pack(records, packed, size - 1), "a pack past the cap was accepted");
    memset(records + 149 * BANK_RECORD_SIZE + 0x400, 0x41, 0x1E00);   /* incompressible */
    for (int i = 0; i < 40; i++) memset(records + i * BANK_RECORD_SIZE + 0x400, 0x41, 0x1E00);
    CHECK(!bank_rle_pack(records, packed, sizeof packed), "an over-full bank was accepted");
    printf("ok over-full: refused\n");

    CHECK(!bank_rle_unpack(file, size - 1, records, why, sizeof why), "a cut file unpacked");
    printf("ok cut file: %s\n", why);
    memcpy(packed, file, size);
    uint32_t a = get32(packed + 4 * 5), b = get32(packed + 4 * 6);
    memcpy(packed + 4 * 5, &b, 4);
    memcpy(packed + 4 * 6, &a, 4);
    CHECK(!bank_rle_unpack(packed, size, records, why, sizeof why), "swapped offsets unpacked");
    printf("ok swapped offsets: %s\n", why);
    printf("all passed\n");
    return 0;
}
