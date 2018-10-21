#include <private/dyndbg_monitor.h>
#include <dyndbg/dyndbg_us.h>

#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>

static void on_trap(int signum);

ddbg_result_t dyndebug_start(void)
{
    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
        return DDBG_CONTEXT_NOT_FOUND;


    struct sigaction sa = {0};
    sa.sa_handler = on_trap;
    int rc = sigaction(SIGTRAP, &sa, NULL);

    /* Run the monitored process code */
    return DDBG_SUCCESS;
}

ddbg_result_t dyndebug_add_breakpoint(ddbg_breakpoint_t *new_bp, void *address,
    ddbg_btype_t type, ddbg_bsize_t size, ddbg_bcallback_t cb, void *priv_arg,
    bool is_hw)
{
    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
        return DDBG_CONTEXT_NOT_FOUND;

    if (!new_bp)
        return DDBG_INVALID_ARGUMENT;

    if (!is_hw)
        return DDBG_SWBP_NOT_IMPLEMENTED;

    if (!cb)
        return DDBG_SWBP_NOT_IMPLEMENTED;

    if (dyndebug_find_breakpoint(address, type, size, false))
        return DDBG_INVALID_ARGUMENT;

    new_bp->address = address;
    new_bp->type = type;
    new_bp->size = size;
    new_bp->callback = cb;
    new_bp->callback_priv_arg = priv_arg;
    new_bp->is_hw = is_hw;
    new_bp->next = context->breakpoints_root;
    context->breakpoints_root = new_bp;

    return dyndebug_enable_breakpoint(new_bp);
}

static void dump_breakpoints(ddbg_context_t *context)
{
    ddbg_breakpoint_t *current = context->breakpoints_root;
    error_print("Breakpoints are:\n");
    while (current)
    {
        error_print("current->address %p\n", current->address);
        current = current->next;
    }
}

ddbg_breakpoint_t *dyndebug_find_breakpoint(void *address, ddbg_btype_t type,
    ddbg_bsize_t size, bool verbose)
{
    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
        return NULL;

    ddbg_breakpoint_t *current = context->breakpoints_root;
    while (current)
    {
        if ((current->address == address) && (current->type == type) &&
                (current->size == size))
            return current;
        current = current->next;
    }
    if (!verbose) return NULL;

    error_print("No breakpoint found at %p\n", address);
    dump_breakpoints(context);
    return NULL;
}

ddbg_result_t dyndebug_remove_breakpoint(ddbg_breakpoint_t *b)
{
    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
        return DDBG_CONTEXT_NOT_FOUND;

    if (!b)
        return DDBG_INVALID_ARGUMENT;

    if (!context->breakpoints_root)
        return DDBG_HWBP_NOT_FOUND;

    /* Disable it before removal */
    dyndebug_disable_breakpoint(b);

    if (b == context->breakpoints_root)
    {
        context->breakpoints_root = b->next;
        return DDBG_SUCCESS;
    }

    ddbg_breakpoint_t *prev = context->breakpoints_root;
    while (prev->next)
    {
        ddbg_breakpoint_t *current = prev->next;
        if ((current->address == b->address) && (current->type == b->type) &&
                (current->size == b->size))
        {
            prev->next = b->next;
            return DDBG_SUCCESS;
        }
        prev = prev->next;
    }
    return DDBG_HWBP_NOT_FOUND;
}

static void dyndebug_send_monitor_request(ddbg_context_t *context,
        ddbg_monitor_request_t *request, ddbg_monitor_response_t *response)
{
    if (write(context->monitor_pipe[1], request, sizeof(*request)) !=
            sizeof(*request))
    {
        error_print("Cannot communicate with the monitor... -- %s\n",
            strerror(errno));
        response->result = DDBG_MONITOR_COMM_FAILURE;
    }
    if (read(context->monitored_pipe[0], response, sizeof(*response)) !=
            sizeof(*response))
    {
        error_print("Cannot read back from the monitor... -- %s\n",
            strerror(errno));
        response->result = DDBG_MONITOR_COMM_FAILURE;
    }
}

static ddbg_result_t dyndebug_enable_disable_breakpoint(ddbg_breakpoint_t *b,
        bool enable)
{
    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
        return DDBG_CONTEXT_NOT_FOUND;

    if (b->enabled == enable)
        return DDBG_SUCCESS;

    ddbg_monitor_request_t request;
    ddbg_monitor_response_t response;
    request.operation = enable ? DDBG_ENABLE_BREAKPOINT : DDBG_DISABLE_BREAKPOINT;
    request.breakpoint.address = b->address;
    request.breakpoint.type = b->type;
    request.breakpoint.size = b->size;
    request.breakpoint.is_hw = b->is_hw;

    dyndebug_send_monitor_request(context, &request, &response);

    /* bookkeeping */
    if (response.result == DDBG_SUCCESS)
        b->enabled = enable;
    return response.result;
}

ddbg_result_t dyndebug_enable_breakpoint(ddbg_breakpoint_t *b)
{
    return dyndebug_enable_disable_breakpoint(b, true);
}

ddbg_result_t dyndebug_disable_breakpoint(ddbg_breakpoint_t *b)
{
    return dyndebug_enable_disable_breakpoint(b, false);
}

ddbg_result_t dyndebug_disable_all_breakpoint()
{
    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
        return DDBG_CONTEXT_NOT_FOUND;

    ddbg_monitor_request_t request;
    ddbg_monitor_response_t response;
    request.operation = DDBG_DISABLE_ALL_BREAKPOINTS;
    dyndebug_send_monitor_request(context, &request, &response);
    if (response.result != DDBG_SUCCESS)
        return response.result;

    /* bookkeeping */
    ddbg_breakpoint_t *current = context->breakpoints_root;
    while (current)
    {
        current->enabled = false;
        current = current->next;
    }
    return DDBG_SUCCESS;
}

static void on_trap(int signum)
{
    if (signum != SIGTRAP)
        return;

    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
    {
        error_print("SIGTRAP signal but context not found!\n");
        return;
    }

    ddbg_monitor_request_t request;
    ddbg_monitor_response_t response;
    request.operation = DDBG_GET_TRIGGERED_BREAKPOINT;
    dyndebug_send_monitor_request(context, &request, &response);
    if (response.result != DDBG_SUCCESS)
    {
        error_print("SIGTRAP signal reason not found!\n");
        return;
    }
    ddbg_breakpoint_t *b = dyndebug_find_breakpoint(response.breakpoint.address,
        response.breakpoint.type, response.breakpoint.size, false);
    if (!b)
    {
        error_print("Breakpoint triggered but not found for (%p, %d, %d)...\n",
            response.breakpoint.address, response.breakpoint.type,
            response.breakpoint.size);
        return;
    }

    b->callback(b);
}
