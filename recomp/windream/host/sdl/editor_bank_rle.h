/*
 * WINDREAM recompilation - DREAMS.DAT's record codec, for the project bank of
 * Develop (editor_bank.c, spec 008 phase 4). Plain C99 with no guest or SDL
 * dependency, so the native harness verify/native/editor_bank_tests.c checks
 * it on the disc's file.
 *
 * The layout (DDAT_Load 0x448f5f reads it whole into _RLE_SAVES 0x633c14,
 * 0x25400 bytes): u32 offset[i] at +4i for i = 0..150, relative to +0x400;
 * offset[150] is the data's end. Bytes 0x25c..0x3ff are padding (zero on the
 * discs). Record i is the bytes [0x400 + offset[i], 0x400 + offset[i + 1]),
 * zero-run packed: "00 k" is k zeros (k <= 255, greedy), any other byte is
 * itself (RLE_UnpackZeros 0x448e25, the packer 0x448d58).
 */
#ifndef WD_EDITOR_BANK_RLE_H
#define WD_EDITOR_BANK_RLE_H
#include <stddef.h>
#include <stdint.h>

#define BANK_RECORDS     150
#define BANK_RECORD_SIZE 0x2200u
#define BANK_HEADER      0x400u
#define BANK_FILE_CAP    0x25400u   /* DDAT_Load's read size: the most DREAMS.DAT can hold */

/* Unpacks the 150 records of file[0..size) into records (150 x 0x2200).
 * Returns 1, or 0 with a reason in why when an offset is out of order or past
 * size, or a record does not decode to exactly 0x2200 bytes (DDAT_LoadRecord
 * clears only the packed length before it unpacks). */
int bank_rle_unpack(const uint8_t* file, size_t size, uint8_t* records, char* why, size_t why_cap);

/* Packs the 150 records into file: the 151 offsets at 0, the data from 0x400,
 * as the retail packer 0x448ec9 lays them out; bytes 0x25c..0x3ff are left as
 * they are. Returns the file size (0x400 + offset[150]), or 0 when that would
 * pass cap (file is then partly written). */
size_t bank_rle_pack(const uint8_t* records, uint8_t* file, size_t cap);

/* One record packed into out (at most cap bytes); returns its length, or 0
 * when it does not fit. */
size_t bank_rle_pack_record(const uint8_t* record, uint8_t* out, size_t cap);

#endif
