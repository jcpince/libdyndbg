#include <dyndbg/dyndbg_us.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

volatile int idx;
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

int main(int argc, const char **argv)
{
    char data[1024];
    int loops = 0, rc;

    test_assert(dyndebug_start(), DDBG_SUCCESS);

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
