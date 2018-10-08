#include <private/dyndbg_monitor.h>

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

static void on_monitored_death(int signum);
static void handle_request(ddbg_context_t *context, ddbg_monitor_request_t *request);

ddbg_context_t *dyndebug_get_context(void)
{
    static ddbg_context_t *context = NULL;
    if (context) return context;

    if (!context)
        context = calloc(1, sizeof(ddbg_context_t));

    if (!context)
    {
        fprintf(stderr, "Cannot create the dyndebug_get_manager singleton, "\
            "malloc(%ld) failed -- %s\n", sizeof(ddbg_context_t), strerror(errno));
        return NULL;
    }
    if (pipe(context->monitor_pipe))
    {
        free(context);
        fprintf(stderr, "Cannot create the monitor pipe -- %s\n", strerror(errno));
        return NULL;
    }

    if (pipe(context->monitored_pipe))
    {
        free(context);
        fprintf(stderr, "Cannot create the monitored pipe -- %s\n", strerror(errno));
        return NULL;
    }

    context->monitor_pid = getpid();
    context->monitored_pid = fork();
    if (context->monitored_pid == -1)
    {
        free(context);
        fprintf(stderr, "Cannot fork our process for monitoring -- %s\n", strerror(errno));
        return NULL;
    }

    if (context->monitored_pid)
    {
        fprintf(stdout, "Monitor process %d, debugged process %d\n",
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
    snprintf(monitor_process_name, 1024, DYNDBG_MONITOR_PREFIX"%s:%d",
        __progname, context->monitored_pid);
    if (prctl(PR_SET_NAME, (unsigned long) monitor_process_name) < 0)
    {
        fprintf(stderr, "Dyndebug monitoring failed to change monitor name!\n");
    }

    context->monitored_process_name = strdup(__progname);

    fclose(stdin);
    close(context->monitor_pipe[1]);
    close(context->monitored_pipe[0]);
    // fcntl(context->monitor_pipe[0], F_SETFL, O_NONBLOCK);

    signal(SIGCHLD, on_monitored_death);

    ddbg_monitor_request_t request;
    while (!interrupted)
    {
        int rc = read(context->monitor_pipe[0], &request, sizeof(ddbg_monitor_request_t));
        if (rc == -1)
        {
            /* Closed pipe ?*/
            fprintf(stderr, "Dyndebug monitoring read failure for %s, abort!\n",
                context->monitored_process_name);
            interrupted = 1;
        } else if (rc == sizeof(ddbg_monitor_request_t))
        {
            /* Handle the request */
            handle_request(context, &request);
        } else if (rc > 0)
        {
            /* Didn't receive a full request, ignore it! */
            fprintf(stderr, "Incomplete dyndebug monitoring request (%d vs %ld)"\
                " for %s, skipped\n", rc, sizeof(ddbg_monitor_request_t),
                context->monitored_process_name);
        } /* else, nothing has been received */
    }
    fprintf(stdout, "Dyndebug monitoring session for %s ended!\n",
        context->monitored_process_name);
}

static void on_monitored_death(int signum)
{
    assert(signum == SIGCHLD);

    ddbg_context_t *context = dyndebug_get_context();
    if (!context)
    {
        fprintf(stdout, "Dyndebug monitoring session (pid %d) received SIGCHLD!\n",
            getpid());
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
            printf("WIFEXITED(%d): waitpid(%d) returned %d -- %s\n",
                WEXITSTATUS(status), context->monitored_pid, rc, strerror(errno));
        } else if (WIFSIGNALED(status))
        {
            printf("WIFSIGNALED(%d): waitpid(%d) returned %d -- %s\n",
                WTERMSIG(status), context->monitored_pid, rc, strerror(errno));
        } else if (WIFSTOPPED(status))
        {
            printf("WIFSTOPPED(%d): waitpid(%d) returned %d -- %s\n",
                WSTOPSIG(status), context->monitored_pid, rc, strerror(errno));
        } else if (WCOREDUMP(status))
        {
            interrupted = 1;
            printf("WCOREDUMP: waitpid(%d) returned %d -- %s\n",
                context->monitored_pid, rc, strerror(errno));
        } else if (WIFCONTINUED(status))
        {
            printf("WIFCONTINUED: waitpid(%d) returned %d -- %s\n",
                context->monitored_pid, rc, strerror(errno));
        } else
            printf("No status: waitpid(%d) returned %d -- %s\n",
                context->monitored_pid, rc, strerror(errno));
    } else
        fprintf(stdout, "Dyndebug monitoring session for %s received SIGCHLD "\
            "but waitpid returned %d!\n", context->monitored_process_name, rc);
}

uint64_t read_drx(pid_t pid, uint8_t x)
{
    assert(x < 8);
    errno = 0;
    uint64_t drx = ptrace( PTRACE_PEEKUSER, pid, offsetof(struct user, u_debugreg[x]), 0);
    int errno_ = errno;
    printf("ptrace( PTRACE_PEEKUSER, %d, offsetof(struct user, u_debugreg[%d])) returned %lx, errno %d -- %s\n",
        pid, x, drx, errno_, strerror(errno_));
    return drx;
}

static void handle_request(ddbg_context_t *context, ddbg_monitor_request_t *request)
{
    printf("New request operation %d\n", request->operation);
    /* Attach the process under debug */
    int rc = ptrace(PTRACE_ATTACH, context->monitored_pid, 0, 0);
    if (rc < 0)
    {
        if (errno == ESRCH)
        {
            fprintf(stderr, "Monitored process %s died, interrupting the session\n",
                context->monitored_process_name);
            interrupted = true;
            return;
        }
        fprintf(stderr, "Cannot attach to the monitored process %s -- %s\n",
            context->monitored_process_name, strerror(errno));
        interrupted = true;
        return;
    }

    /* Now, wait for the process to be stopped */
    for (int attempts = 0 ; attempts < PTRACE_STOP_ATTEMPTS ; attempts++)
    {
        int status;
        rc = waitpid(context->monitored_pid, &status, WNOHANG);
        if (rc == context->monitored_pid) break;
        if (rc == -1)
        {
            fprintf(stderr, "Cannot wait for to the monitored process %s -- %s\n",
                context->monitored_process_name, strerror(errno));
        }
    }

    /* Interpret the request */
    ddbg_monitor_response_t response;
    switch (request->operation)
    {
        case DDBG_ENABLE_BREAKPOINT:
            fprintf(stdout, "%s:%d Enable breakpoint at %p\n", __func__, __LINE__,
                    request->breakpoint.address);
            read_drx(context->monitored_pid, 6);
            fprintf(stdout, "%s:%d Enable breakpoint at %p\n", __func__, __LINE__,
                    request->breakpoint.address);
            response.result = DDBG_SUCCESS;
            break;
        case DDBG_DISABLE_BREAKPOINT:
            fprintf(stdout, "Disable breakpoint at %p\n",
                    request->breakpoint.address);
            response.result = DDBG_SUCCESS;
            break;
        case DDBG_DISABLE_ALL_BREAKPOINTS:
            fprintf(stdout, "Disable all the breakpoints\n");
            response.result = DDBG_SUCCESS;
            break;
        case DDBG_GET_TRIGGERED_BREAKPOINT:
            fprintf(stdout, "Get the triggered breakpoint\n");
            response.result = DDBG_SUCCESS;
            break;
        default:
            fprintf(stdout, "Unknown operation %d\n", request->operation);
            response.result = DDBG_MONITOR_REQUEST_UNKNOWN;
            break;
    }

    /* Finally detach the process under debug */
    rc = ptrace(PTRACE_DETACH, context->monitored_pid, 0, 0);
    if (rc < 0)
    {
        if (errno == ESRCH)
        {
            fprintf(stderr, "Monitored process %s died before we could detach it,"\
                " interrupting the session\n", context->monitored_process_name);
            interrupted = true;
            return;
        }
        fprintf(stderr, "Cannot attach to the monitored process %s -- %s\n",
            context->monitored_process_name, strerror(errno));
        interrupted = true;
        return;
    }

    /* Send the response */
    if (write(context->monitored_pipe[1], &response, sizeof(response)) !=
            sizeof(response))
    {
        fprintf(stderr, "Cannot answer the monitored process %s -- %s\n",
            context->monitored_process_name, strerror(errno));
        interrupted = true;
        return;
    }
}

#if 0
void interpret_request(ddbg_monitor_request_t *request,
        ddbg_monitor_response_t *response)
{
    if (!request->breakpoint.is_hw)
    {
        response->result = DDBG_SWBP_NOT_IMPLEMENTED;
        return;
    }

    /* Disable the breakpoint */
    if (!request->enabled)
    {
        for (; hw_idx < HW_BREAKPOINTS_COUNT ; hw_idx++)
        {
            ddbg_breakpoint_t *active_hwb = context->active_hw_breakpoints[hw_idx];
            if  (active_hwb && (active_hwb->address == request->address) &&
                (active_hwb->type == request->type) && (active_hwb->size == request->size))
            {
                context->active_hw_breakpoints[hw_idx] = NULL;
                response.result = DDBG_SUCCESS;
                return;
            }
        }
        response.result = DDBG_HWBP_NOT_FOUND;
        return;
    }

    /* Enable the breakpoint */
    for (; hw_idx < HW_BREAKPOINTS_COUNT ; hw_idx++)
    {
        if (!context->active_hw_breakpoints[hw_idx])
            break;
    }
    if (hw_idx == HW_BREAKPOINTS_COUNT)
    {
        response.result = DDBG_ALL_HWBP_BUSY;
        return;
    }

    /* Found a slot for our new breakpoint */
    context->active_hw_breakpoints[hw_idx] = b;
    b->hw_index = hw_idx;
}
#endif
