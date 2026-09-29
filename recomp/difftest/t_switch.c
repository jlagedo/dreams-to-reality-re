/*
 * Switches and loops shaped like the game's collision code (0x40CA00): a big
 * frame, calls in a loop, and a dense switch whose jump table Watcom places in
 * the middle of the function with the case code after it.
 */
#include <stdio.h>
#include <string.h>

static unsigned long H;
static void mix(const void* p, int n) {
    const unsigned char* b = (const unsigned char*)p;
    while (n--) H = (H ^ *b++) * 16777619UL;
}
static void mi(long v) { mix(&v, sizeof v); }
static void md(double v) { mix(&v, sizeof v); }
static void begin(void) { H = 2166136261UL; }
static void end(const char* name) { printf("%-12s %08lx\n", name, H); }

static unsigned long seed = 777;
static long rnd(void) { seed = seed * 1103515245UL + 12345UL; return (long)(seed >> 1); }

typedef struct Tri { long v[3]; long n; float d; struct Tri* next; } Tri;
static Tri tris[64];
static Tri* head;
static volatile long vl[64];

static Tri* next_tri(Tri* t) { return t ? t->next : head; }
static long classify(Tri* t, long x) { return (t->v[0] ^ x) % 5 - 2; }   /* -2..2 */

/* The game's shape: loop over a list from a call, switch on (call result + 2). */
static long collide(long x, long y) {
    long acc = 0, hits[5], k;
    char big[200];
    Tri* t = 0;
    memset(hits, 0, sizeof hits);
    memset(big, (int)x, sizeof big);
    for (;;) {
        t = next_tri(t);
        if (!t) break;
        switch (classify(t, x) + 2) {
        case 0: acc += t->v[1]; hits[0]++; break;
        case 1: acc -= t->v[2] >> 3; hits[1]++; if (acc & 1) continue; break;
        case 2: acc ^= y; hits[2]++; break;
        case 3: acc = acc * 3 + big[t->n & 127]; hits[3]++; break;
        case 4: acc += (long)(t->d * 16.0f); hits[4]++; break;
        }
        acc += t->n;
    }
    for (k = 0; k < 5; k++) acc = acc * 31 + hits[k];
    return acc;
}

/* Switch in a nested loop, with char cases and fallthrough. */
static long nested(const char* s, long n) {
    long i, j, r = 0;
    for (i = 0; i < n; i++)
        for (j = 0; s[j]; j++) {
            switch (s[j]) {
            case 'a': r += 1;
            case 'b': r += 2; break;
            case 'c': r *= 3; break;
            case 'd': r -= i; break;
            case 'e': r ^= j; break;
            case 'f': r += i * j; break;
            case 'g': if (r > 1000) goto out; break;
            case 'h': r >>= 1; break;
            default: r++;
            }
        }
out:
    return r;
}

/* Several switches in one function; one returns from inside the switch. */
static long multi(long a, long b) {
    long r = 0;
    switch (a & 7) {
    case 0: r = b; break; case 1: r = -b; break; case 2: r = b * 2; break;
    case 3: r = b / 3; break; case 4: return b ^ 0x5555; case 5: r = b << 2; break;
    case 6: r = b % 7; break; case 7: r = ~b; break;
    }
    switch ((b >> 4) & 15) {
    case 0: case 1: case 2: r += 10; break;
    case 3: case 4: r -= 20; break;
    case 5: case 6: case 7: case 8: r *= 5; break;
    case 12: r = 0; break;
    default: r++;
    }
    return r;
}

/* A state machine: switch inside while, state kept across iterations. */
static long machine(long steps, long x) {
    int state = 0;
    long v = x;
    while (steps-- > 0) {
        switch (state) {
        case 0: v += 3; state = (v & 1) ? 1 : 2; break;
        case 1: v *= 3; state = 3; break;
        case 2: v -= 7; state = (v & 4) ? 4 : 0; break;
        case 3: v ^= v >> 3; state = 5; break;
        case 4: v += steps; state = 1; break;
        case 5: v = v * 5 + 1; state = (v & 8) ? 0 : 2; break;
        }
    }
    return v * 8 + state;
}

int main(void) {
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < 64; i++) {
        tris[i].v[0] = rnd(); tris[i].v[1] = rnd() & 0xFFFF; tris[i].v[2] = rnd();
        tris[i].n = rnd() & 255; tris[i].d = (float)(rnd() % 1000) / 37.0f;
        tris[i].next = i < 63 ? &tris[i + 1] : 0;
        vl[i] = rnd();
    }
    head = &tris[0];
    begin(); for (i = 0; i < 64; i++) mi(collide(vl[i], vl[(i + 1) & 63])); end("collide");
    printf("  collide(1,2)=%ld\n", collide(1, 2));
    begin(); for (i = 0; i < 16; i++) mi(nested("abcdefghxyz" + (i % 5), i + 1)); end("nested");
    begin(); for (i = 0; i < 64; i++) mi(multi(vl[i], vl[(i + 3) & 63] & 0xFFFF)); end("multi");
    begin(); for (i = 0; i < 64; i++) mi(machine(i * 3, vl[i] & 0xFFFF)); end("machine");
    md(0.0);
    return 0;
}
