#include <dyndbg/dyndbg_us.h>

#include <stdio.h>

#define __USE_GNU
#include <ucontext.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

volatile uint64_t idx;
volatile int b0_count = 0;
volatile int b1_count = 0;
volatile int b2_count = 0;

#define test_assert(under_test, expect)                                         \
    {                                                                           \
        int res = under_test;                                                   \
        if (res != expect) {                                                    \
            fprintf(stderr, "" # under_test " returned %d instead of %d!!,"     \
            " test failed\n", res, expect);                                     \
            assert(false);                                                      \
        }                                                                       \
    }

#define test_assert2(under_test, expect1, expect2)                              \
    {                                                                           \
        int res = under_test;                                                   \
        if (res != expect1 && res != expect2) {                                 \
            fprintf(stderr, "" # under_test " returned %d instead of %d or %d!!"\
             " test failed\n", res, expect1, expect2);                          \
            assert(false);                                                      \
        }                                                                       \
    }

void on_b0_triggerred()
{
    test_assert(idx, 122);
    b0_count++;
}

void on_b1_triggerred()
{
    test_assert2(idx, 256, 257);
    b1_count++;
}

void on_b2_triggerred()
{
    b2_count++;
}

int func(void)
{
    return 0;
}

void crash_callback(int signum, void *_ucontext)
{
    ucontext_t *ucontext = _ucontext;

    if (signum == SIGFPE)
        ucontext->uc_mcontext.gregs[REG_RCX] = 1;
    else if (signum == SIGILL)
        ucontext->uc_mcontext.gregs[REG_RIP] += 2;
    else
    {
        fprintf(stderr, "%s(%p) called, set rax from 0x%llx to %p.\n", __func__,
            ucontext, ucontext->uc_mcontext.gregs[REG_RAX], &idx);
        ucontext->uc_mcontext.gregs[REG_RAX] = (uint64_t)&idx;
    }
}

int main(int argc, const char **argv)
{
    char data[1024];
    int loops = 0, rc;

    dyndebug_install_crash_handler(crash_callback);

    /* SIGSEGV */
    register long *prax __asm__("rax") = (long*)5;
    prax[0] = 0xDEADDEAD;
    prax = (long*)5;
    rc = prax[0];

    /* SIGBUS */
    /* Set x86 alignment check */
    __asm__("pushf\n"
            "xorl $0x40000,(%rsp)\n"
            "popf\n");
    prax = (long*)&data[3];
    __asm__("movq $0x5, (%rax)\n");
    /* Reset x86 alignment check for printing. */
    __asm__("pushf\n"
            "xorl $0x40000,(%rsp)\n"
            "popf\n");

    /* SIGFPE */
    register long rcx __asm__("rcx") = 0;
    __asm__("cltd\n idiv %rcx\n cltq\n");

    /* SIGILL */
    __asm__("ud2\n");

    test_assert(dyndebug_start_monitor(), DDBG_SUCCESS);

    ddbg_breakpoint_t _b0, *b0 = &_b0, _b1, *b1 = &_b1, _b2, *b2 = &_b2;
    test_assert(dyndebug_disable_breakpoint(b1), DDBG_HWBP_NOT_FOUND);
    test_assert(dyndebug_add_breakpoint(b0, &data[122], DDBG_BREAK_DATA_WRITE,
            DDBG_BREAK_1BYTE, on_b0_triggerred, NULL, true), DDBG_SUCCESS);
    test_assert(dyndebug_add_breakpoint(b1, &data[256], DDBG_BREAK_DATA_RDWR,
            DDBG_BREAK_2BYTES, on_b1_triggerred, NULL, true), DDBG_SUCCESS);

    while(loops < 2)
    {
        data[idx]++;
        usleep(50);
        if (++idx >= 1024)
        {
            idx = 0;
            loops++;
        }
    }

    test_assert(b0_count, 2);
    test_assert(b1_count, 8);

    test_assert(dyndebug_disable_breakpoint(b1), DDBG_SUCCESS);
    loops = 0;
    while(loops < 2)
    {
        data[idx]++;
        usleep(50);
        if (++idx >= 1024)
        {
            idx = 0;
            loops++;
        }
    }

    test_assert(b0_count, 4);
    test_assert(b1_count, 8);

    test_assert(dyndebug_remove_breakpoint(b0), DDBG_SUCCESS);
    loops = 0;
    while(loops < 2)
    {
        data[idx]++;
        usleep(50);
        if (++idx >= 1024)
        {
            idx = 0;
            loops++;
        }
    }

    test_assert(b0_count, 4);
    test_assert(b1_count, 8);

    test_assert(dyndebug_enable_breakpoint(b1), DDBG_SUCCESS);
    loops = 0;
    while(loops < 2)
    {
        data[idx]++;
        usleep(50);
        if (++idx >= 1024)
        {
            idx = 0;
            loops++;
        }
    }

    test_assert(b0_count, 4);
    test_assert(b1_count, 16);

    test_assert(dyndebug_remove_breakpoint(b0), DDBG_HWBP_NOT_FOUND);
    test_assert(dyndebug_remove_breakpoint(b1), DDBG_SUCCESS);
    loops = 0;
    while(loops < 2)
    {
        data[idx]++;
        usleep(50);
        if (++idx >= 1024)
        {
            idx = 0;
            loops++;
        }
    }

    test_assert(b0_count, 4);
    test_assert(b1_count, 16);

    test_assert(dyndebug_disable_breakpoint(b0), DDBG_HWBP_NOT_FOUND);
    test_assert(dyndebug_disable_breakpoint(b1), DDBG_HWBP_NOT_FOUND);

    rc = func();
    test_assert(b2_count, 0);

    test_assert(dyndebug_add_breakpoint(b2, func, DDBG_BREAK_INSTRUCTION,
            DDBG_BREAK_1BYTE, on_b2_triggerred, NULL, true), DDBG_SUCCESS);
    rc = func();
    test_assert(b2_count, 1);

    test_assert(dyndebug_disable_breakpoint(b2), DDBG_SUCCESS);
    rc = func();
    test_assert(b2_count, 1);

    printf("All tests succeeded!!\nThat's all folks!\n");
    return 0;
}
