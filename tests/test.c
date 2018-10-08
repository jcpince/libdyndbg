#include <dyndbg/dyndbg_us.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

volatile int idx;

void sigint_handler(int signum)
{
    printf("Interrupted\n");
}

void on_b0_triggerred()
{
    printf("b0 triggered\n");
}

int main(int argc, const char **argv)
{
    char data[1024];
    signal(SIGINT, sigint_handler);
    ddbg_result_t result = dyndebug_start();
    printf("dyndebug_start() returned %d\n", result);

    ddbg_breakpoint_t _b0, *b0 = &_b0;
    result = dyndebug_add_breakpoint(b0, &data[122], DDBG_BREAK_DATA_WRITE,
            DDBG_BREAK_1BYTE, on_b0_triggerred, NULL, true);
    printf("dyndebug_add_breakpoint(b0, %p, DWR, 1) returned %d\n", &data[122], result);

    result = dyndebug_enable_breakpoint(b0);
    printf("dyndebug_enable_breakpoint(b0) returned %d\n", result);

    sleep(2);
    printf("That's all folks!\n");
    return 0;
}
