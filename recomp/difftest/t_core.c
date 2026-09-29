/*
 * Differential test for the recompiler: built with Watcom 11.0 (the Windows game's
 * compiler), run natively for the expected output, then lifted and run through
 * the recompiled runtime. Each group prints one hash over every result it
 * computed, plus a few sample values, so a mismatch names the construct.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>

static unsigned long H;
static void mix(const void* p, int n) {
    const unsigned char* b = (const unsigned char*)p;
    while (n--) H = (H ^ *b++) * 16777619UL;
}
static void mi(long v) { mix(&v, sizeof v); }
static void mu(unsigned long v) { mix(&v, sizeof v); }
static void md(double v) { mix(&v, sizeof v); }
static void mf(float v) { mix(&v, sizeof v); }
static void begin(void) { H = 2166136261UL; }
static void end(const char* name) { printf("%-12s %08lx\n", name, H); }

static unsigned long seed = 12345;
static long rnd(void) { seed = seed * 1103515245UL + 12345UL; return (long)(seed >> 1); }
static double rndd(void) { return (double)(rnd() % 200001 - 100000) / 997.0; }

/* volatile inputs so nothing folds at compile time */
static volatile double vd[64];
static volatile float vf[64];
static volatile long vl[64];
static volatile unsigned char vb[64];
static volatile short vs[64];

static void setup(void) {
    int i;
    for (i = 0; i < 64; i++) {
        vd[i] = rndd();
        vf[i] = (float)rndd();
        vl[i] = rnd() - 0x40000000L;
        vb[i] = (unsigned char)rnd();
        vs[i] = (short)rnd();
    }
    vd[0] = 0.0; vd[1] = -0.0; vd[2] = 1.0; vd[3] = -1.0; vd[4] = 0.5; vd[5] = 2.0;
    vd[6] = vd[7] = 3.25; vl[0] = 0; vl[1] = -1; vl[2] = 1; vl[3] = 0x7fffffffL;
    vs[0] = 0; vs[1] = -1; vs[2] = 32767; vs[3] = -32768; vb[0] = 0; vb[1] = 255;
}

static void t_fcmp(void) {
    int i, j;
    begin();
    for (i = 0; i < 64; i++)
        for (j = 0; j < 64; j++) {
            double a = vd[i], b = vd[j];
            int r = (a < b) | (a <= b) << 1 | (a == b) << 2 | (a > b) << 3 | (a >= b) << 4 | (a != b) << 5;
            float fa = vf[i], fb = vf[j];
            r |= (fa < fb) << 6 | (fa >= fb) << 7 | (fa < 0.0f) << 8 | (a > 0.0) << 9;
            mi(r);
        }
    end("fcmp");
}

static void t_farith(void) {
    int i, j;
    begin();
    for (i = 0; i < 64; i++)
        for (j = 1; j < 64; j++) {
            double a = vd[i], b = vd[j];
            md(a + b); md(a - b); md(b - a); md(a * b);
            if (b != 0.0) { md(a / b); }
            if (a != 0.0) { md(b / a); }
            mf(vf[i] * vf[j] + vf[i]);
            md(a * 3 - b / 7 + (a - b) * (a + b));
        }
    end("farith");
}

static void t_fconv(void) {
    int i;
    begin();
    for (i = 0; i < 64; i++) {
        double a = vd[i] * 1000.0;
        mi((long)a); mi((short)a); mi((int)(float)a); mu((unsigned long)fabs(a));
        md((double)vl[i]); md((double)vs[i]); md((double)vb[i]); mf((float)vl[i]);
        md(floor(vd[i])); md(ceil(vd[i])); mi((long)floor(vd[i] + 0.5));
    }
    printf("  (long)-2.7=%ld (long)2.7=%ld floor(-2.5)=%g ceil(-2.5)=%g\n",
           (long)(vd[3] * 2.7), (long)(vd[2] * 2.7), floor(-2.5 * vd[2]), ceil(-2.5 * vd[2]));
    end("fconv");
}

#define FM(name, expr) do { int i; begin(); for (i = 0; i < 64; i++) { double a = vd[i], p = fabs(a); md(expr); } end("  " name); } while (0)
static void t_fmath(void) {
    FM("sqrt", sqrt(p)); FM("sin", sin(a)); FM("cos", cos(a)); FM("atan2", atan2(a, vd[(i + 7) & 63]));
    FM("atan", atan(a)); FM("pow.5", pow(p, 0.5)); FM("pow2", pow(a, 2.0)); FM("pow3", pow(a, 3.0));
    FM("pow1.5", pow(p + 1, 1.5)); FM("exp", exp(a / 64)); FM("log", p > 0 ? log(p) : 0.0);
    FM("log10", p > 0 ? log10(p) : 0.0); FM("fmod", fmod(a, 3.7)); FM("tan", tan(a / 64));
    FM("fabs", fabs(a)); FM("asin", asin(a / 1000)); FM("acos", acos(a / 1000));
    printf("  pow(-2,2)=%g pow(9,0.5)=%g sin(1)=%.15g cos(1)=%.15g atan2(1,-1)=%.15g\n",
           pow(-2.0 * vd[2], 2.0), pow(9.0 * vd[2], 0.5), sin(vd[2]), cos(vd[2]), atan2(vd[2], vd[3]));
    printf("  tan(.3)=%.15g fmod(7.5,2)=%.15g asin(.5)=%.15g exp(1)=%.15g log(10)=%.15g\n",
           tan(0.3 * vd[2]), fmod(7.5 * vd[2], 2.0), asin(0.5 * vd[2]), exp(vd[2]), log(10.0 * vd[2]));
}

static void t_int(void) {
    int i, j;
    begin();
    for (i = 0; i < 64; i++)
        for (j = 0; j < 64; j++) {
            long a = vl[i], b = vl[j];
            unsigned long ua = (unsigned long)a, ub = (unsigned long)b;
            mi(a + b); mi(a - b); mi(a * b); mu(ua * ub);
            if (b) { mi(a / b); mi(a % b); }
            if (ub) { mu(ua / ub); mu(ua % ub); }
            mi(a >> (j & 31)); mu(ua >> (j & 31)); mu(ua << (i & 31));
            mi(a / 8); mi(a / 65536); mi(a % 16); mi((a * 3) >> 15);
            mi(a < b); mi(ua < ub); mi(a <= b); mi(ua >= ub);
            mi(labs(a)); mi(a & b); mi(a | b); mi(a ^ ~b);
        }
    end("int32");
}

static void t_narrow(void) {
    int i, j;
    begin();
    for (i = 0; i < 64; i++)
        for (j = 0; j < 64; j++) {
            unsigned char a = vb[i], b = vb[j];
            short s = vs[i], t = vs[j];
            unsigned short us = (unsigned short)vs[i], ut = (unsigned short)vs[j];
            mi((unsigned char)(a * b)); mi((short)(s * t)); mi((unsigned short)(us * ut));
            if (b) { mi((unsigned char)(a / b)); mi((unsigned char)(a % b)); }
            if (t) { mi((short)(s / t)); mi((short)(s % t)); }
            if (ut) { mi((unsigned short)(us / ut)); mi((unsigned short)(us % ut)); }
            mi((signed char)a >> 2); mi(s >> 3); mi(us >> 3); mi((short)(s << 2));
            mi(a < b); mi(s < t); mi(us < ut); mi((signed char)a < (signed char)b);
        }
    end("int8/16");
}

static void t_fixed(void) {
    int i, j;
    begin();
    for (i = 0; i < 64; i++)
        for (j = 0; j < 64; j++) {
            long a = vl[i] >> 8, b = vl[j] >> 12;
            mi((long)(((double)a * b) / 65536.0));
            mi((a * (b >> 8)) >> 8);
            if (b >> 4) mi((a << 4) / (b >> 4));
            mi(a / 32768); mi(a >> 15); mi(-a / 4);
        }
    end("fixed");
}

static int sw_dense(int x) {
    switch (x) {
    case 0: return 11; case 1: return 22; case 2: return 33; case 3: return 44;
    case 4: return 55; case 5: return 66; case 6: return 77; case 7: return 88;
    case 8: return 99; case 9: return 111; default: return -1;
    }
}
static int sw_sparse(long x) {
    switch (x) {
    case -100: return 1; case 7: return 2; case 1000: return 3; case 65536: return 4;
    case 12345678: return 5; default: return 0;
    }
}
static int (*fns[4])(int) = { sw_dense, abs, sw_dense, abs };

static void t_control(void) {
    int i;
    begin();
    for (i = -5; i < 20; i++) { mi(sw_dense(i)); mi(fns[i & 3](i - 7)); }
    for (i = 0; i < 64; i++) mi(sw_sparse(vl[i] & 7 ? vl[i] : 1000));
    mi(sw_sparse(-100)); mi(sw_sparse(65536)); mi(sw_sparse(12345678));
    end("control");
}

static jmp_buf jb;
static int depth(int n) { if (n == 0) longjmp(jb, 42); return depth(n - 1) + 1; }
static long va_sum(int n, ...) {
    va_list ap; long s = 0; va_start(ap, n);
    while (n--) s = s * 31 + va_arg(ap, long);
    va_end(ap); return s;
}
static double va_dsum(int n, ...) {
    va_list ap; double s = 0; va_start(ap, n);
    while (n--) s = s * 1.5 + va_arg(ap, double);
    va_end(ap); return s;
}
struct big { long a[9]; double d; char c; };
static struct big mkbig(long x) { struct big b; int i; for (i = 0; i < 9; i++) b.a[i] = x * i; b.d = x * 0.25; b.c = (char)x; return b; }

static void t_misc(void) {
    int r;
    char buf[128];
    struct big b;
    begin();
    r = setjmp(jb);
    if (!r) depth(10);
    printf("  setjmp=%d va_sum=%ld va_dsum=%.10g\n", r, va_sum(5, 1L, 2L, 3L, vl[5], vl[6]), va_dsum(4, 1.0, vd[8], 3.5, vd[9]));
    mi(r);
    mi(va_sum(5, 1L, 2L, 3L, vl[5], vl[6]));
    md(va_dsum(4, 1.0, vd[8], 3.5, vd[9]));
    b = mkbig(vl[10]); mix(&b, sizeof b);
    printf("  big=%ld %ld %g %d\n", b.a[1], b.a[8], b.d, b.c);
    sprintf(buf, "%d %ld %u %x %5.2f %e %g %s", -7, vl[11], 3000000000U, 0xbeef, vd[12], vd[13], vd[14], "ok");
    mix(buf, (int)strlen(buf));
    printf("  %s\n", buf);
    mi(strcmp("abc", "abd")); mi(memcmp(buf, "-7", 2)); mi((long)strlen(buf));
    printf("  strcmp=%d memcmp=%d strlen=%d\n", strcmp("abc", "abd"), memcmp(buf, "-7", 2), (int)strlen(buf));
    memset(buf, 'x', 100); memcpy(buf + 3, buf + 50, 20); memmove(buf + 1, buf, 30); mix(buf, 100);
    { unsigned long h0 = H; begin(); mix(buf, 100); printf("  memops %08lx\n", H); H = h0; }
    mi(atoi("  -1234")); md(atof("3.5e2")); mi(strtol("0x1F", 0, 16));
    printf("  atoi=%d atof=%g strtol=%ld\n", atoi("  -1234"), atof("3.5e2"), strtol("0x1F", 0, 16));
    end("misc");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* keep output up to a crash */
    setup();
    t_fcmp();
    t_farith();
    t_fconv();
    t_fmath();
    t_int();
    t_narrow();
    t_fixed();
    t_control();
    t_misc();
    return 0;
}
