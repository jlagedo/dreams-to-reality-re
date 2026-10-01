#ifndef WD_DISPLAY_SCRIPT_PARSE_H
#define WD_DISPLAY_SCRIPT_PARSE_H
#include <stdint.h>
#include <string.h>

/* Exact ASCII grammar shared by environment parsing and CPU tests. No scanf
 * overflow, optional signs on times, whitespace, exponents or empty entries. */
static int wd_script_unsigned(const char** input, uint32_t maximum, uint32_t* value) {
    const char* p = *input;
    uint32_t result = 0;
    if (*p < '0' || *p > '9') return 0;
    do {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (result > maximum / 10 || (result == maximum / 10 && digit > maximum % 10)) return 0;
        result = result * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    *input = p;
    *value = result;
    return 1;
}
static int wd_script_dimension(const char** input, int* value) {
    const char* p = *input;
    uint32_t result;
    if (*p < '1' || *p > '9' || !wd_script_unsigned(&p, 16384, &result) || result < 64) return 0;
    *input = p; *value = (int)result;
    return 1;
}
static int wd_script_coordinate(const char** input, int* value) {
    const char* p = *input;
    int negative = *p == '-';
    if (negative) ++p;
    uint32_t result;
    if (!wd_script_unsigned(&p, 32767, &result)) return 0;
    *input = p; *value = negative ? -(int)result : (int)result;
    return 1;
}
static int wd_script_end(const char** input) {
    if (**input == ',') {
        if (!(*input)[1]) return 0;
        ++*input;
        return 1;
    }
    return **input == 0;
}
static int wd_script_resize(const char** input, uint32_t* ms, int* width, int* height) {
    const char* p = *input;
    if (!wd_script_unsigned(&p, UINT32_MAX, ms) || *p++ != ':' ||
        !wd_script_dimension(&p, width) || *p++ != 'x' ||
        !wd_script_dimension(&p, height) || !wd_script_end(&p)) return 0;
    *input = p;
    return 1;
}
static int wd_script_mouse(const char** input, uint32_t* ms, int* kind, int* x, int* y) {
    static const char* names[] = {"move", "left-down", "left-up", "right-down", "right-up"};
    const char* p = *input;
    if (!wd_script_unsigned(&p, UINT32_MAX, ms) || *p++ != ':') return 0;
    *kind = -1;
    for (int i = 0; i < 5; ++i) {
        size_t length = strlen(names[i]);
        if (!strncmp(p, names[i], length) && p[length] == ':') {
            *kind = i; p += length + 1; break;
        }
    }
    if (*kind < 0 || !wd_script_coordinate(&p, x) || *p++ != ':' ||
        !wd_script_coordinate(&p, y) || !wd_script_end(&p)) return 0;
    *input = p;
    return 1;
}
#endif
