/*
 * Development control channel - JSON reader and writer (ctl_json.h).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ctl_json.h"

/* ---- reading ---- */
static char* skip(char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* p is just past an opening quote. Unescape the string where it stands, end
 * it with a NUL and return the position after the closing quote; NULL if the
 * string is malformed. */
static char* string(char* p, const char** out) {
    char* w = p;
    *out = p;
    for (;;) {
        unsigned char c = (unsigned char)*p++;
        if (!c) return NULL;
        if (c == '"') { *w = 0; return p; }
        if (c != '\\') { *w++ = (char)c; continue; }
        c = (unsigned char)*p++;
        switch (c) {
        case '"': case '\\': case '/': *w++ = (char)c; break;
        case 'b': *w++ = '\b'; break;
        case 'f': *w++ = '\f'; break;
        case 'n': *w++ = '\n'; break;
        case 'r': *w++ = '\r'; break;
        case 't': *w++ = '\t'; break;
        case 'u': {   /* the basic plane, as UTF-8; never longer than the six characters it replaces */
            unsigned v = 0;
            for (int i = 0; i < 4; i++, p++) {
                unsigned char h = (unsigned char)*p;
                unsigned d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10u
                           : h >= 'A' && h <= 'F' ? h - 'A' + 10u : 16u;
                if (d > 15) return NULL;
                v = v * 16 + d;
            }
            if (v < 0x80) *w++ = (char)v;
            else if (v < 0x800) { *w++ = (char)(0xC0 | v >> 6); *w++ = (char)(0x80 | (v & 0x3F)); }
            else { *w++ = (char)(0xE0 | v >> 12); *w++ = (char)(0x80 | (v >> 6 & 0x3F)); *w++ = (char)(0x80 | (v & 0x3F)); }
            break;
        }
        default: return NULL;
        }
    }
}

int json_parse(char* line, JsonField* fields, int max) {
    int n = 0;
    char* p = skip(line);
    if (*p++ != '{') return -1;
    p = skip(p);
    if (*p == '}') return *skip(p + 1) ? -1 : 0;
    for (;;) {
        const char* key;
        p = skip(p);
        if (*p++ != '"' || !(p = string(p, &key))) return -1;
        p = skip(p);
        if (*p++ != ':' || n == max) return -1;
        p = skip(p);
        JsonField* f = &fields[n++];
        f->key = key; f->str = NULL; f->num = 0;
        if (*p == '"') {
            if (!(p = string(p + 1, &f->str))) return -1;
            f->type = 's';
        } else if (!strncmp(p, "true", 4)) { f->type = 'b'; f->num = 1; p += 4; }
        else if (!strncmp(p, "false", 5)) { f->type = 'b'; p += 5; }
        else if (!strncmp(p, "null", 4)) { f->type = '0'; p += 4; }
        else {   /* a number; '{' and '[' fail here, so nesting is refused */
            char* end;
            f->num = strtod(p, &end);
            if (end == p) return -1;
            f->type = 'n';
            p = end;
        }
        p = skip(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') return *skip(p + 1) ? -1 : n;
        return -1;
    }
}

const JsonField* json_get(const JsonField* fields, int n, const char* key) {
    for (int i = 0; i < n; i++)
        if (!strcmp(fields[i].key, key)) return &fields[i];
    return NULL;
}

int json_u64(const JsonField* field, uint64_t* out) {
    if (!field) return 0;
    if (field->type == 'n') {
        if (field->num < 0 || field->num > 18446744073709549568.0) return 0;
        *out = (uint64_t)field->num;
        return 1;
    }
    if (field->type == 's' && field->str[0] && field->str[0] != '-') {
        char* end;
        *out = strtoull(field->str, &end, 0);
        return end != field->str && !*end;
    }
    return 0;
}

const char* json_text(const JsonField* field) {
    return field && field->type == 's' ? field->str : NULL;
}

/* ---- writing ---- */
static void put(JsonOut* o, const char* s, size_t n) {
    if (o->length + n + 1 > o->capacity) {
        size_t capacity = o->capacity ? o->capacity : 256;
        while (o->length + n + 1 > capacity) capacity *= 2;
        char* text = (char*)realloc(o->text, capacity);
        if (!text) abort();
        o->text = text; o->capacity = capacity;
    }
    memcpy(o->text + o->length, s, n);
    o->length += n;
    o->text[o->length] = 0;
}

static void quoted(JsonOut* o, const char* s, int guest) {
    put(o, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char esc[8];
        if (c == '"' || c == '\\') { esc[0] = '\\'; esc[1] = (char)c; put(o, esc, 2); }
        else if (c < 0x20 || (guest && c > 0x7F)) put(o, esc, (size_t)snprintf(esc, sizeof esc, "\\u%04x", c));
        else put(o, (const char*)&c, 1);
    }
    put(o, "\"", 1);
}

/* What precedes a value: a comma after an earlier one, and the key in an object. */
static void lead(JsonOut* o, const char* key) {
    if (o->comma) put(o, ",", 1);
    if (key) { quoted(o, key, 0); put(o, ":", 1); }
    o->comma = 1;
}

void jo_open(JsonOut* o, const char* key, char bracket) {
    lead(o, key);
    put(o, &bracket, 1);
    o->comma = 0;
}
void jo_close(JsonOut* o, char bracket) {
    put(o, &bracket, 1);
    o->comma = 1;
}
void jo_int(JsonOut* o, const char* key, int64_t value) {
    char number[32];
    lead(o, key);
    put(o, number, (size_t)snprintf(number, sizeof number, "%lld", (long long)value));
}
void jo_bool(JsonOut* o, const char* key, int value) {
    lead(o, key);
    put(o, value ? "true" : "false", value ? 4 : 5);
}
void jo_str(JsonOut* o, const char* key, const char* value, int guest) {
    lead(o, key);
    quoted(o, value, guest);
}
void jo_hex(JsonOut* o, const char* key, const uint8_t* bytes, size_t n) {
    static const char digits[] = "0123456789abcdef";
    lead(o, key);
    put(o, "\"", 1);
    for (size_t i = 0; i < n; i++) {
        char pair[2] = { digits[bytes[i] >> 4], digits[bytes[i] & 15] };
        put(o, pair, 2);
    }
    put(o, "\"", 1);
}
