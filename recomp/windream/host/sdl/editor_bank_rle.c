/*
 * WINDREAM recompilation - DREAMS.DAT's record codec (editor_bank_rle.h).
 */
#include "editor_bank_rle.h"

#include <stdio.h>
#include <string.h>

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int bank_rle_unpack(const uint8_t* file, size_t size, uint8_t* records, char* why, size_t why_cap) {
    if (size < BANK_HEADER) {
        snprintf(why, why_cap, "%u bytes: shorter than the 0x400-byte header", (unsigned)size);
        return 0;
    }
    for (int i = 0; i < BANK_RECORDS; i++) {
        uint32_t at = get32(file + 4 * i), end = get32(file + 4 * (i + 1));
        if (end < at || BANK_HEADER + (size_t)end > size) {
            snprintf(why, why_cap, "record %d: offsets 0x%x..0x%x out of order or past the data", i, at, end);
            return 0;
        }
        const uint8_t* src = file + BANK_HEADER + at;
        uint8_t* dst = records + (size_t)i * BANK_RECORD_SIZE;
        size_t n = end - at, out = 0;
        for (size_t k = 0; k < n; k++) {
            if (src[k]) {
                if (out >= BANK_RECORD_SIZE) {
                    out = BANK_RECORD_SIZE + 1;
                    break;
                }
                dst[out++] = src[k];
                continue;
            }
            if (++k >= n) {
                snprintf(why, why_cap, "record %d: a zero-run escape with no count at its end", i);
                return 0;
            }
            if (out + src[k] > BANK_RECORD_SIZE) {
                out = BANK_RECORD_SIZE + 1;
                break;
            }
            memset(dst + out, 0, src[k]);
            out += src[k];
        }
        if (out != BANK_RECORD_SIZE) {
            snprintf(why, why_cap, "record %d does not decode to 0x2200 bytes", i);
            return 0;
        }
    }
    return 1;
}

size_t bank_rle_pack_record(const uint8_t* record, uint8_t* out, size_t cap) {
    size_t n = 0;
    for (size_t i = 0; i < BANK_RECORD_SIZE;) {
        if (record[i]) {
            if (n + 1 > cap) return 0;
            out[n++] = record[i++];
            continue;
        }
        size_t run = 0;
        while (i < BANK_RECORD_SIZE && !record[i] && run < 255) {
            i++;
            run++;
        }
        if (n + 2 > cap) return 0;
        out[n++] = 0;
        out[n++] = (uint8_t)run;
    }
    return n;
}

size_t bank_rle_pack(const uint8_t* records, uint8_t* file, size_t cap) {
    if (cap < BANK_HEADER) return 0;
    uint32_t at = 0;
    for (int i = 0; i < BANK_RECORDS; i++) {
        put32(file + 4 * i, at);
        size_t n = bank_rle_pack_record(records + (size_t)i * BANK_RECORD_SIZE, file + BANK_HEADER + at,
                                        cap - BANK_HEADER - at);
        if (!n) return 0;
        at += (uint32_t)n;
    }
    put32(file + 4 * BANK_RECORDS, at);
    return BANK_HEADER + at;
}
