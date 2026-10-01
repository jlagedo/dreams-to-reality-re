/*
 * Development control channel - the JSON it needs, and no more.
 *
 * Reading: one flat object per line, whose values are strings, numbers, true,
 * false or null. Nested objects and arrays are refused.
 * Writing: objects and arrays of strings, integers and booleans, appended to a
 * growing buffer.
 */
#ifndef WD_CTL_JSON_H
#define WD_CTL_JSON_H

#include <stddef.h>
#include <stdint.h>

/* ---- reading ---- */
typedef struct {
    const char* key;
    char type;            /* 's' string, 'n' number, 'b' boolean, '0' null */
    const char* str;      /* 's': the text (unescaped, in the line's own buffer) */
    double num;           /* 'n': the value; 'b': 0 or 1 */
} JsonField;

/* Parse {"key": value, ...} in place (the line is overwritten: keys and string
 * values end up NUL-terminated inside it). Returns the number of fields, or
 * -1 for anything that is not a flat object or has more than max fields. */
int json_parse(char* line, JsonField* fields, int max);
const JsonField* json_get(const JsonField* fields, int n, const char* key);
/* An unsigned integer given as a number or as a string ("0x661e04", "42"). */
int json_u64(const JsonField* field, uint64_t* out);
const char* json_text(const JsonField* field);   /* the string, or NULL */

/* ---- writing ---- */
typedef struct {
    char* text;           /* NUL-terminated; free() it */
    size_t length, capacity;
    int comma;            /* the next key or element needs a comma first */
} JsonOut;

void jo_open(JsonOut* o, const char* key, char bracket);    /* '{' or '['; key NULL in an array or at the top */
void jo_close(JsonOut* o, char bracket);                    /* '}' or ']' */
void jo_int(JsonOut* o, const char* key, int64_t value);
void jo_bool(JsonOut* o, const char* key, int value);
/* Host text (UTF-8) passes through. Guest text is code page 1252 bytes, which
 * are not UTF-8: guest != 0 writes each byte above 0x7F as \u00XX. */
void jo_str(JsonOut* o, const char* key, const char* value, int guest);
void jo_hex(JsonOut* o, const char* key, const uint8_t* bytes, size_t n);

#endif /* WD_CTL_JSON_H */
