#include <private/x86_debug_registers.h>
#include <private/dyndbg_monitor.h>
#include <dyndbg/dyndbg_us.h>

#include <sys/ptrace.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>

volatile bool interrupted = 0;

static void on_monitored_signal(int signum);
static void handle_request(ddbg_context_t *, ddbg_monitor_request_t *);
static void setup_hw_breakpoint(pid_t , ddbg_monitor_request_t *,
        ddbg_monitor_response_t *);
static void prepare_trig_breakpt_response(pid_t , ddbg_monitor_response_t *);

ddbg_context_t *dyndebug_get_context(void)
{
    static ddbg_context_t *context = NULL;
    if (context) return context;

    assert(sizeof(x86_breakpoint_control_t) == sizeof(uint64_t));
    assert(sizeof(x86_breakpoint_status_t) == sizeof(uint64_t));

    if (!context)
        context = calloc(1, sizeof(ddbg_context_t));

    if (!context)
    {
        error_print("Cannot create the dyndebug_get_manager singleton, "\
            "malloc(%ld) failed -- %s\n", sizeof(ddbg_context_t), strerror(errno));
        return NULL;
    }
    if (pipe(context->monitor_pipe))
    {
        free(context);
        error_print("Cannot create the monitor pipe -- %s\n", strerror(errno));
        return NULL;
    }

    if (pipe(context->monitored_pipe))
    {
        free(context);
        error_print("Cannot create the monitored pipe -- %s\n", strerror(errno));
        return NULL;
    }

    context->monitor_pid = getpid();
    context->monitored_pid = fork();
    if (context->monitored_pid == -1)
    {
        free(context);
        error_print("Cannot fork our process for monitoring -- %s\n", strerror(errno));
        return NULL;
    }

    if (context->monitored_pid)
    {
        debug_print("Monitor process %d, debugged process %d\n",
            context->monitor_pid, context->monitored_pid);
        dyndebug_run_monitor(context);
        free(context);
        exit(0);
    }
    return context;
}

void dyndebug_run_monitor(ddbg_context_t *context)
{
    char monitor_process_name[1024];
    extern char *__progname;
    int rc;

    snprintf(monitor_process_name, 1024, DYNDBG_MONITOR_PREFIX"%s:%d",
        __progname, context->monitored_pid);
    if (prctl(PR_SET_NAME, (unsigned long) monitor_process_name) < 0)
    {
        error_print("Dyndebug monitoring failed to change monitor name!\n");
    }

    context->monitored_process_name = strdup(__progname);

    fclose(stdin);
    close(context->monitor_pipe[1]);
    close(context->monitored_pipe[0]);
    // fcntl(context->monitor_pipe[0], F_SETFL, O_NONBLOCK);

    //signal(SIGCHLD, on_monitored_signal);
    struct sigaction sa = {0};
    sa.sa_handler = on_monitored_signal;
    rc = sigaction(SIGCHLD, &sa, NULL);
    debug_print("sigaction(%d, sa, NULL) returned %d\n", SIGCHLD, rc);

    ddbg_monitor_request_t request;
    while (!interrupted)
    {
        errno = 0;
        rc = read(context->monitor_pipe[0], &request, sizeof(ddbg_monitor_request_t));
        if (errno == EAGAIN)
            continue;
        if (rc == -1)
        {
            /* Closed pipe ?*/
            error_print("Dyndebug monitoring read failure for %s, abort!\n",
                context->monitored_process_name);
            interrupted = 1;
        } else if (rc == sizeof(ddbg_monitor_request_t))
        {
            /* Handle the request */
            handle_request(context, &request);
        } else if (rc > 0)
        {
            /* Didn't receive a full request, ignore it! */
            error_print("Incomplete dyndebug monitoring request (%d vs %ld)"\
                " for %s, skipped\n", rc, sizeof(ddbg_monitor_request_t),
                context->monitored_process_name);
        } /* else, nothing has been received */
    }
    debug_print("Dyndebug monitoring session for %s ended!\n",
        context->monitored_process_name);
}

static void on_monitored_signal(int signum)
{
    assert(signum == SIGCHLD);

    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
    {
        error_print("SIGCHLD received (pid %d) but no context found!\n", getpid());
        return;
    }
    int status;
    errno = 0;
    int rc = waitpid(context->monitored_pid, &status, WNOHANG);
    if (rc == context->monitored_pid)
    {
        if (WIFEXITED(status))
        {
            interrupted = 1;
            debug_print("WIFEXITED(%d): waitpid(%d) returned %d -- %s\n",
                WEXITSTATUS(status), context->monitored_pid, rc, strerror(errno));
        } else if (WIFSIGNALED(status))
        {
            debug_print("WIFSIGNALED(%d): waitpid(%d) returned %d -- %s\n",
                WTERMSIG(status), context->monitored_pid, rc, strerror(errno));
        } else if (WIFSTOPPED(status))
        {
            debug_print("WIFSTOPPED(%d): waitpid(%d) returned %d -- %s\n",
                WSTOPSIG(status), context->monitored_pid, rc, strerror(errno));
        } else if (WCOREDUMP(status))
        {
            interrupted = 1;
            debug_print("WCOREDUMP: waitpid(%d) returned %d -- %s\n",
                context->monitored_pid, rc, strerror(errno));
        } else if (WIFCONTINUED(status))
        {
            debug_print("WIFCONTINUED: waitpid(%d) returned %d -- %s\n",
                context->monitored_pid, rc, strerror(errno));
        } else
            debug_print("No status: waitpid(%d) returned %d -- %s\n",
                context->monitored_pid, rc, strerror(errno));
    } else
        debug_print("Dyndebug monitoring session for %s received SIGCHLD "\
            "but waitpid returned %d!\n", context->monitored_process_name, rc);
}

static void handle_request(ddbg_context_t *context, ddbg_monitor_request_t *request)
{
    debug_print("New request operation %d\n", request->operation);
    /* Attach the process under debug */
    int rc = ptrace(PTRACE_ATTACH, context->monitored_pid, 0, 0);
    if (rc < 0)
    {
        if (errno == ESRCH)
        {
            error_print("Monitored process %s died, interrupting the session\n",
                context->monitored_process_name);
            interrupted = true;
            return;
        }
        error_print("Cannot attach to the monitored process %s -- %s\n",
            context->monitored_process_name, strerror(errno));
        interrupted = true;
        return;
    }

    /* Now, wait for the process to be stopped */
    // for (int attempts = 0 ; attempts < PTRACE_STOP_ATTEMPTS ; attempts++)
    // {
    int status;
        rc = waitpid(context->monitored_pid, &status, 0); //WNOHANG);
        if (rc == context->monitored_pid)
        {
            debug_print("Got the pid...\n");
            //break;
        }
        if (rc == -1)
        {
            error_print("Cannot wait for to the monitored process %s -- %s\n",
                context->monitored_process_name, strerror(errno));
        }
    //}

    /* Interpret the request */
    ddbg_monitor_response_t response;
    x86_breakpoint_control_t bp_control = {0};
    switch (request->operation)
    {
        case DDBG_ENABLE_BREAKPOINT:
            debug_print("%s:%d Enable breakpoint at %p\n", __func__, __LINE__,
                    request->breakpoint.address);
            // read_drx(context->monitored_pid, 6);
            // rc = write_drx(context->monitored_pid, 1, (uint64_t)request->breakpoint.address);
            // bp_control.l1 = 1;
            // bp_control.le = 1;
            // bp_control.ge = 1;
            // bp_control.rw1 = DDBG_BREAK_DATA_RDWR;
            // bp_control.len1 = DDBG_BREAK_1BYTE;
            // rc = write_drx(context->monitored_pid, 7, ((uint64_t*)&bp_control)[0]);
            setup_hw_breakpoint(context->monitored_pid, request, &response);
            debug_print("%s:%d Enable breakpoint at %p\n", __func__, __LINE__,
                    request->breakpoint.address);
            //response.result = DDBG_SUCCESS;
            break;
        case DDBG_DISABLE_BREAKPOINT:
            debug_print("Disable breakpoint at %p\n",
                    request->breakpoint.address);
            response.result = DDBG_SUCCESS;
            break;
        case DDBG_DISABLE_ALL_BREAKPOINTS:
            debug_print("Disable all the breakpoints\n");
            response.result = DDBG_SUCCESS;
            break;
        case DDBG_GET_TRIGGERED_BREAKPOINT:
            debug_print("Get the triggered breakpoint\n");
            prepare_trig_breakpt_response(context->monitored_pid, &response);
            break;
        default:
            debug_print("Unknown operation %d\n", request->operation);
            response.result = DDBG_MONITOR_REQUEST_UNKNOWN;
            break;
    }

    /* Finally detach the process under debug */
    rc = ptrace(PTRACE_DETACH, context->monitored_pid, 0, 0);
    if (rc < 0)
    {
        if (errno == ESRCH)
        {
            error_print("Monitored process %s died before we could detach it,"\
                " interrupting the session\n", context->monitored_process_name);
            interrupted = true;
            return;
        }
        error_print("Cannot attach to the monitored process %s -- %s\n",
            context->monitored_process_name, strerror(errno));
        interrupted = true;
        return;
    }

    /* Send the response */
    if (write(context->monitored_pipe[1], &response, sizeof(response)) !=
            sizeof(response))
    {
        error_print("Cannot answer the monitored process %s -- %s\n",
            context->monitored_process_name, strerror(errno));
        interrupted = true;
        return;
    }
}

static void setup_hw_breakpoint(pid_t pid, ddbg_monitor_request_t *request,
        ddbg_monitor_response_t *response)
{
    x86_breakpoint_control_t control = x86_read_dr_control(pid);
    if (!X86_DBG_CONTROL_VALID(control))
    {
        response->result = (ddbg_result_t)errno;
        return;
    }
    x86_breakpoint_register_t breakpoint;
    if (!control.l0)
    {
        control.l0 = 1;
        control.rw0 = request->breakpoint.type;
        control.len0 = request->breakpoint.size;
        breakpoint = X86_HW_BREAKPOINT_0;
    } else if (!control.l1)
    {
        control.l1 = 1;
        control.rw1 = request->breakpoint.type;
        control.len1 = request->breakpoint.size;
        breakpoint = X86_HW_BREAKPOINT_1;
    } else if (!control.l2)
    {
        control.l2 = 1;
        control.rw2 = request->breakpoint.type;
        control.len2 = request->breakpoint.size;
        breakpoint = X86_HW_BREAKPOINT_2;
    } else if (!control.l3)
    {
        control.l3 = 1;
        control.rw3 = request->breakpoint.type;
        control.len3 = request->breakpoint.size;
        breakpoint = X86_HW_BREAKPOINT_3;
    }
    else
    {
        response->result = DDBG_ALL_HWBP_BUSY;
        return;
    }
    if (x86_write_drx(pid, breakpoint, (uint64_t)request->breakpoint.address))
    {
        response->result = (ddbg_result_t)errno;
        return;
    }
    response->result = (x86_write_dr_control(pid, control) == 0 ?
        DDBG_SUCCESS : (ddbg_result_t)errno);
}

static void prepare_trig_breakpt_response(pid_t pid,
        ddbg_monitor_response_t *response)
{
    x86_breakpoint_status_t status = x86_read_dr_status(pid);
    if (!X86_DBG_STATUS_VALID(status))
    {
        response->result = (ddbg_result_t)errno;
        return;
    }
    /* Clear the status register */
    x86_breakpoint_status_t clear_mask = {.rtm=1};
    if (x86_write_dr_status(pid, clear_mask))
    {
        response->result = (ddbg_result_t)errno;
        return;
    }

    x86_breakpoint_control_t control = x86_read_dr_control(pid);
    if (!X86_DBG_CONTROL_VALID(control))
    {
        response->result = (ddbg_result_t)errno;
        return;
    }
    x86_breakpoint_register_t reg;
    ddbg_btype_t type;
    ddbg_bsize_t size;
    if (status.b0)
    {
        reg = X86_HW_BREAKPOINT_0;
        type = control.rw0;
        size = control.len0;
    }
    else if (status.b1)
    {
        reg = X86_HW_BREAKPOINT_1;
        type = control.rw1;
        size = control.len1;
    }
    else if (status.b2)
    {
        reg = X86_HW_BREAKPOINT_2;
        type = control.rw2;
        size = control.len2;
    }
    else if (status.b3)
    {
        reg = X86_HW_BREAKPOINT_3;
        type = control.rw3;
        size = control.len3;
    }
    else
    {
        error_print("Trap exception but no breakpt triggered, status is 0x%lx\n",
            *((uint64_t*)&status));
        return;
    }
    uint64_t drx = x86_read_drx(pid, reg);
    response->breakpoint.address = (void *)drx;
    response->breakpoint.type = type;
    response->breakpoint.size = size;
    response->breakpoint.is_hw = true;
    response->result = DDBG_SUCCESS;
}
