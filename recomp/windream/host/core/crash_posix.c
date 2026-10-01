/*
 * crash_posix.c - the crash handler on Linux and macOS: signal handlers.
 * See crash_report.h; the report itself is crash_report.c.
 *
 * SIGSEGV, SIGBUS, SIGFPE and SIGILL are reported, then the signal is raised
 * again with its default action, so the process dies of it and a core dump or
 * a debugger still gets its turn.
 *
 * The host stack is raw frames from backtrace(): module and offset, plus a
 * name where the dynamic symbol table has one (link with -rdynamic for the
 * sub_XXXXXXXX names). There are no line numbers, so no guest instruction per
 * frame as on Windows; "current lifted function" in the report still names
 * the innermost one.
 *
 * The handler runs on an alternate stack, which is per thread: only the thread
 * that installs (the main thread) has one, and only its stack overflows are
 * reported. Faults on other threads are reported on their own stack.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* REG_RIP, REG_ERR */
#endif
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ucontext.h>

#if defined(__has_include)
#if __has_include(<execinfo.h>)   /* glibc and macOS; not musl */
#include <execinfo.h>
#define CRASH_HAVE_BACKTRACE 1
#endif
#endif

#include "crash_report.h"
#include "crash_internal.h"

static const void* fault_pc(void* uctx) {
    ucontext_t* uc = (ucontext_t*)uctx;
#if defined(__linux__) && defined(__x86_64__)
    return (const void*)(uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__linux__) && defined(__aarch64__)
    return (const void*)(uintptr_t)uc->uc_mcontext.pc;
#elif defined(__APPLE__) && defined(__aarch64__)
    /* the accessor strips the pointer-authentication bits on arm64e */
    return (const void*)(uintptr_t)__darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
#elif defined(__APPLE__) && defined(__x86_64__)
    return (const void*)(uintptr_t)uc->uc_mcontext->__ss.__rip;
#else
    (void)uc;
    return NULL;
#endif
}

/* 1 a write, 0 a read, -1 when this host does not say. */
static int fault_is_write(void* uctx) {
#if defined(__linux__) && defined(__x86_64__)
    /* the page-fault error code: bit 1 is set for a write */
    return (((ucontext_t*)uctx)->uc_mcontext.gregs[REG_ERR] & 2) != 0;
#else
    (void)uctx;
    return -1;
#endif
}

static void on_fault(int sig, siginfo_t* si, void* uctx) {
    static atomic_flag once = ATOMIC_FLAG_INIT;
    if (!atomic_flag_test_and_set(&once)) {   /* else: a fault while reporting */
        const char* name;
        switch (sig) {
        /* SIGBUS is what macOS sends for most bad pointers; one name, so the
         * reports read like the Windows ones. */
        case SIGSEGV: case SIGBUS: name = "access violation"; break;
        case SIGFPE:               name = "integer divide by zero"; break;
        default:                   name = "illegal instruction"; break;
        }
        /* The report opens the symbol file and allocates the first time. A
         * fault inside malloc holds the allocator's lock, and the report
         * would wait on it forever: the alarm ends the process instead. */
        signal(SIGALRM, SIG_DFL);
        alarm(10);
        /* si_code <= 0 is a signal someone sent (kill, raise): si_addr is
         * then a pid, not an address. */
        int has_addr = (sig == SIGSEGV || sig == SIGBUS) && si->si_code > 0;
        crash_report_fault(name, fault_pc(uctx), NULL, has_addr,
                           has_addr ? (uintptr_t)si->si_addr : 0, has_addr ? fault_is_write(uctx) : 0);
#ifdef CRASH_HAVE_BACKTRACE
        /* Printed for a stack overflow too: the walk only reads the stack. */
        void* frames[64];
        int n = backtrace(frames, 64);
        fprintf(stderr, "  host stack (raw frames; innermost first):\n");
        fflush(stderr);
        backtrace_symbols_fd(frames, n, 2);   /* straight to the descriptor: no malloc */
#endif
        recomp_report_state("state at fault");
        fflush(stderr);
    }

    /* Report, then let it die: back to the default action and raise it again.
     * The signal is blocked in here, so it is delivered as this returns. */
    signal(sig, SIG_DFL);
    raise(sig);
}

void recomp_install_crash_handler(void) {
    static int installed = 0;
    if (installed) return;
    installed = 1;

    /* An overflowed stack has no room for the handler. SIGSTKSZ is not a
     * constant on glibc 2.34 and later, hence the check at run time. */
    static char stack[128 * 1024];
    stack_t ss;
    ss.ss_sp = stack;
    ss.ss_size = sizeof stack;
    ss.ss_flags = 0;
    size_t need = (size_t)SIGSTKSZ;
    if (need > sizeof stack) {
        void* big = malloc(need);   /* never freed */
        if (big) { ss.ss_sp = big; ss.ss_size = need; }
    }
    sigaltstack(&ss, NULL);

#ifdef CRASH_HAVE_BACKTRACE
    /* glibc loads libgcc on the first backtrace(): do that here, not inside a
     * fault, where its malloc could deadlock. */
    void* warm[1];
    backtrace(warm, 1);
#endif

    struct sigaction sa;
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGFPE, SIGILL };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        sigaction(sigs[i], &sa, NULL);
}
