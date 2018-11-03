#ifndef __DYNDEBUG_US__
#define __DYNDEBUG_US__

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

struct ddbg_breakpoint_;

typedef void (*ddbg_bcallback_t)(struct ddbg_breakpoint_ *bp);
typedef void (*ddbg_crash_callback_t)(int signum, void *ucontext);

typedef enum
{
    DDBG_SUCCESS = 100,
    DDBG_CONTEXT_NOT_FOUND,
    DDBG_START_FAILURE,
    DDBG_INVALID_ARGUMENT,
    DDBG_ALL_HWBP_BUSY,
    DDBG_SWBP_NOT_IMPLEMENTED,
    DDBG_HWBP_NOT_FOUND,
    DDBG_BP_ALREADY_EXISTS,
    DDBG_MONITOR_REQUEST_FAILURE,
    DDBG_MONITOR_RESPONSE_FAILURE,
    DDBG_MONITOR_COMM_FAILURE,
    DDBG_MONITOR_REQUEST_UNKNOWN,
    DDBG_SYSTEM_ERROR,
} ddbg_result_t;

typedef enum
{
    DDBG_BREAK_INSTRUCTION = 0,
    DDBG_BREAK_DATA_WRITE,
    DDBG_BREAK_DATA_IO_RDWR, /* CR4.DE shall be set */
    DDBG_BREAK_DATA_RDWR,
} ddbg_btype_t;

typedef enum
{
    DDBG_BREAK_1BYTE = 0,
    DDBG_BREAK_2BYTES,
    DDBG_BREAK_8BYTES,
    DDBG_BREAK_4BYTES,
} ddbg_bsize_t;

typedef struct ddbg_breakpoint_
{
    struct ddbg_breakpoint_ *next;
    ddbg_bcallback_t        callback;
    void                    *callback_priv_arg;
    void                    *address;
    ddbg_btype_t            type:2;
    ddbg_bsize_t            size:2;
    bool                    is_hw;
    bool                    enabled;
} ddbg_breakpoint_t;

ddbg_result_t dyndebug_start_monitor(void);
ddbg_result_t dyndebug_install_crash_handler(ddbg_crash_callback_t cb);
ddbg_result_t dyndebug_add_breakpoint(ddbg_breakpoint_t *new_bp, void *address,
    ddbg_btype_t type, ddbg_bsize_t size, ddbg_bcallback_t cb, void *priv_arg,
    bool is_hw);
ddbg_breakpoint_t *dyndebug_find_breakpoint(void *address, ddbg_btype_t type,
    ddbg_bsize_t size, bool verbose);
ddbg_result_t dyndebug_remove_breakpoint(ddbg_breakpoint_t *b);
ddbg_result_t dyndebug_enable_breakpoint(ddbg_breakpoint_t *b);
ddbg_result_t dyndebug_disable_breakpoint(ddbg_breakpoint_t *b);
ddbg_result_t dyndebug_disable_all_breakpoint();

#endif /* __DYNDEBUG_US__ */
