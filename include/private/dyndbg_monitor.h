#ifndef __PRIV_DYNDEBUG_MONITOR__
#define __PRIV_DYNDEBUG_MONITOR__

#include <dyndbg/dyndbg_us.h>

#include <stdio.h>

#define DYNDBG_MONITOR_PREFIX   "dyndbg_monitor_"
#define HW_BREAKPOINTS_COUNT    4
#define PTRACE_STOP_ATTEMPTS    4

#if 0
#define debug_print(fmt, args...)   printf("%s:%d: "fmt, __func__, __LINE__, ##args)
#else
#define debug_print(fmt, args...)
#endif
#define info_print(fmt, args...)    printf("%s:%d: "fmt, __func__, __LINE__, ##args)
#define error_print(fmt, args...)   fprintf(stderr, "%s:%d: "fmt, __func__, __LINE__, ##args)

typedef enum
{
    DDBG_ENABLE_BREAKPOINT,
    DDBG_DISABLE_BREAKPOINT,
    DDBG_DISABLE_ALL_BREAKPOINTS,
    DDBG_GET_TRIGGERED_BREAKPOINT,
} ddbg_monitor_op_t;

typedef struct
{
    ddbg_monitor_op_t       operation;
    union
    {
        struct
        {
            void            *address;
            ddbg_btype_t    type:8;
            ddbg_bsize_t    size:8;
            bool            is_hw;
        } breakpoint;
    };
} ddbg_monitor_request_t;

typedef struct
{
    ddbg_result_t           result;
    union
    {
        struct
        {
            void            *address;
            ddbg_btype_t    type:8;
            ddbg_bsize_t    size:8;
            bool            is_hw;
        } breakpoint;
    };
} ddbg_monitor_response_t;

typedef struct
{
    char                    *monitored_process_name;
    ddbg_breakpoint_t       *breakpoints_root;
    int                     monitor_pipe[2];
    int                     monitored_pipe[2];
    pid_t                   monitor_pid;
    pid_t                   monitored_pid;
} ddbg_context_t;

ddbg_context_t *dyndebug_get_context(void);
void dyndebug_run_monitor(ddbg_context_t *context);

#endif /* __PRIV_DYNDEBUG_MONITOR__ */
