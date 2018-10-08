#include <private/dyndbg_monitor.h>
#include <dyndbg/dyndbg_us.h>

#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>

ddbg_result_t dyndebug_start(void)
{
    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
        return DDBG_CONTEXT_NOT_FOUND;

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

    if (dyndebug_find_breakpoint(address, type, size))
        return DDBG_INVALID_ARGUMENT;

    new_bp->address = address;
    new_bp->type = type;
    new_bp->size = size;
    new_bp->callback = cb;
    new_bp->callback_priv_arg = priv_arg;
    new_bp->is_hw = is_hw;
    new_bp->next = context->breakpoints_root;
    context->breakpoints_root = new_bp;

    return DDBG_SUCCESS;
}

ddbg_breakpoint_t *dyndebug_find_breakpoint(void *address, ddbg_btype_t type,
    ddbg_bsize_t size)
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
        fprintf(stderr, "Cannot communicate with the monitor... -- %s\n",
            strerror(errno));
        response->result = DDBG_MONITOR_COMM_FAILURE;
    }
    if (read(context->monitored_pipe[0], response, sizeof(*response)) !=
            sizeof(*response))
    {
        fprintf(stderr, "Cannot read back from the monitor... -- %s\n",
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

    ddbg_monitor_request_t request;
    ddbg_monitor_response_t response;
    request.operation = enable ? DDBG_ENABLE_BREAKPOINT : DDBG_DISABLE_BREAKPOINT;
    request.breakpoint.address = b->address;
    request.breakpoint.type = b->type;
    request.breakpoint.size = b->size;
    request.breakpoint.is_hw = b->is_hw;

    dyndebug_send_monitor_request(context, &request, &response);
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
    return response.result;
}

void on_trap(int signum)
{
    if (signum != SIGTRAP)
        return;

    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
    {
        fprintf(stderr, "SIGTRAP signal but context not found!\n");
        return;
    }

    ddbg_monitor_request_t request;
    ddbg_monitor_response_t response;
    request.operation = DDBG_GET_TRIGGERED_BREAKPOINT;
    dyndebug_send_monitor_request(context, &request, &response);
    if (response.result != DDBG_SUCCESS)
    {
        fprintf(stderr, "SIGTRAP signal reason not found!\n");
        return;
    }
    ddbg_breakpoint_t *b = dyndebug_find_breakpoint(response.breakpoint.address,
        response.breakpoint.type, response.breakpoint.size);
    if (!b)
    {
        fprintf(stderr, "Breakpoint triggered but not found for (%p, %d, %d)...\n",
            response.breakpoint.address, response.breakpoint.type,
            response.breakpoint.size);
        return;
    }

    b->callback(b);
}
