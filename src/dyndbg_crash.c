#include <dyndbg/dyndbg_us.h>
#include <private/dyndbg_monitor.h>

#define __USE_GNU
#include <ucontext.h>

#include <execinfo.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define MAX_BACKTRACE_DEPTH 256
#define MAX_STACKDUMP_DEPTH 512

#define min(a, b) ((a) < (b) ? (a) : (b))

enum X86_TRAPNO
{
    TRAPONO_DIV0                = 0x0,
    TRAPONO_ILLEGAL_INSTRUCTION = 0x6,
    TRAPONO_PAGE_FAULT          = 0xe,
    TRAPONO_ALIGNMENT_CHECK     = 0x11,
};

enum X86_ERROR_BITS
{
    ERROR_BITS_PAGE_PRESENT     = 1 << 0,
    ERROR_BITS_WRITE            = 1 << 1,
    ERROR_BITS_USER             = 1 << 2,
    ERROR_BITS_RESWRITE         = 1 << 3,
    ERROR_BITS_INST_FETCH       = 1 << 4,
};

static ddbg_crash_callback_t crash_callback = NULL;
extern void dyndebug_on_crash(int signum, siginfo_t *info, void *ucontext);

void set_crash_callback(ddbg_crash_callback_t cb)
{
    crash_callback = cb;
}

ddbg_result_t dyndebug_install_crash_handler(ddbg_crash_callback_t cb)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = dyndebug_on_crash;
    sa.sa_flags = SA_SIGINFO;
    int rc = sigaction(SIGSEGV, &sa, NULL);
    if (rc)
    {
        rc = errno;
        error_print("Cannot install the segmentaton fault handler -- %s\n",
            strerror(errno));
        errno = rc;
        return DDBG_SYSTEM_ERROR;
    }
    rc = sigaction(SIGILL, &sa, NULL);
    if (rc)
    {
        rc = errno;
        error_print("Cannot install the illegal instruction handler -- %s\n",
            strerror(errno));
        errno = rc;
        return DDBG_SYSTEM_ERROR;
    }
    rc = sigaction(SIGFPE, &sa, NULL);
    if (rc)
    {
        rc = errno;
        error_print("Cannot install the floatting point exception handler -- %s\n",
            strerror(errno));
        errno = rc;
        return DDBG_SYSTEM_ERROR;
    }
    rc = sigaction(SIGBUS, &sa, NULL);
    if (rc)
    {
        rc = errno;
        error_print("Cannot install the bus error handler -- %s\n",
            strerror(errno));
        errno = rc;
        return DDBG_SYSTEM_ERROR;
    }

    set_crash_callback(cb);
    return DDBG_SUCCESS;
}

void dump_stack(mcontext_t *mcontext)
{
    greg_t *r = mcontext->gregs;
    fprintf(stderr, "\nPartial stack dump (upper bytes more recent):\n");
    int stack_size = min(MAX_STACKDUMP_DEPTH, r[REG_RBP] - r[REG_RSP]);
    uint64_t *stack_bottom = (uint64_t*)((uint8_t *)r[REG_RSP] - stack_size);
    fprintf(stderr, "Dumping %d bytes from %p to %p\n", stack_size, stack_bottom,
        stack_bottom+stack_size);
    stack_size /= sizeof(void*);
    for ( ; stack_size > 0 ; stack_size--)
    {
        fprintf(stderr, "%p: ", stack_bottom);
        void *array[1];
        array[0] = (void*)(*stack_bottom);
        backtrace_symbols_fd(array, 1, fileno(stderr));
        stack_bottom++;
    }

}

void dump_registers(mcontext_t *mcontext)
{
    greg_t *r = mcontext->gregs;
    fprintf(stderr, "\nGeneric registers:\n");
    fprintf(stderr, "ERR  0x%016llx, TRAPNO 0x%016llx, OMSK 0x%016llx\n", r[REG_ERR], r[REG_TRAPNO], r[REG_OLDMASK]);
    fprintf(stderr, "CS  0x%04llx, GS  0x%04llx, FS 0x%04llx\n", r[REG_CSGSFS]&0xffff, (r[REG_CSGSFS]>>16)&0xffff, (r[REG_CSGSFS]>>32)&0xffff);
    fprintf(stderr, "RAX  0x%016llx, RBX  0x%016llx, RCX  0x%016llx\n", r[REG_RAX], r[REG_RBX], r[REG_RCX]);
    fprintf(stderr, "RDX  0x%016llx, RSI  0x%016llx, RDI  0x%016llx\n", r[REG_RDX], r[REG_RSI], r[REG_RDI]);
    fprintf(stderr, "R08  0x%016llx, R09  0x%016llx, R10  0x%016llx\n", r[REG_R8], r[REG_R9], r[REG_R10]);
    fprintf(stderr, "R11  0x%016llx, R12  0x%016llx, R13  0x%016llx\n", r[REG_R11], r[REG_R12], r[REG_R13]);
    fprintf(stderr, "R14  0x%016llx, R15  0x%016llx\n", r[REG_R14], r[REG_R15]);
    fprintf(stderr, "RBP  0x%016llx, RSP  0x%016llx\n", r[REG_RBP], r[REG_RSP]);
    fprintf(stderr, "RIP  0x%016llx, EFL  0x%016llx, CR2  0x%016llx\n", r[REG_RIP], r[REG_EFL], r[REG_CR2]);

    fprintf(stderr, "\nSymbols associated with the registers:\n");
    backtrace_symbols_fd((void*)r, NGREG, fileno(stderr));
}

void print_error(uint64_t error)
{
    bool prior = false;
    if (error&ERROR_BITS_PAGE_PRESENT)
    {
        fprintf(stderr, "'page violation'");
        prior = true;
    }
    if (error&ERROR_BITS_WRITE)
    {
        if (prior) fprintf(stderr, ", ");
        fprintf(stderr, "'write access'");
    }
    if (!(error&ERROR_BITS_WRITE))
    {
        if (prior) fprintf(stderr, ", ");
        fprintf(stderr, "'read access'");
    }
    if (error&ERROR_BITS_RESWRITE)
    {
        if (prior) fprintf(stderr, ", ");
        fprintf(stderr, "'res write access'");
    }
    if (error&ERROR_BITS_INST_FETCH)
    {
        if (prior) fprintf(stderr, ", ");
        fprintf(stderr, "'instruction fetch'");
    }
}

void print_fault(void *addr, mcontext_t *mcontext)
{
    greg_t *r = mcontext->gregs;
    uint64_t trapno = r[REG_TRAPNO];
    switch (trapno)
    {
        case TRAPONO_DIV0:
            fprintf(stderr, "\n\nDivision by 0 caught at instruction 0x%016llx:\n",
                r[REG_RIP]);
            break;
        case TRAPONO_ILLEGAL_INSTRUCTION:
            fprintf(stderr, "\n\nIllegal instruction caught at instruction 0x%016llx:\n",
                r[REG_RIP]);
            break;
        case TRAPONO_PAGE_FAULT:
            fprintf(stderr, "\n\nPage fault (");
            print_error(r[REG_ERR]);
            fprintf(stderr, ") accessing %p caught at 0x%016llx:\n", addr, r[REG_RIP]);
            break;
        case TRAPONO_ALIGNMENT_CHECK:
            fprintf(stderr, "\n\nAlignment check caught at instruction 0x%016llx:\n",
                r[REG_RIP]);
            break;
    }
}

void dyndebug_on_crash(int signum, siginfo_t *info, void *_ucontext)
{
    ucontext_t *ucontext = _ucontext;

    /* Disable alignment check if set, it will be restored upon signal return */
    __asm__("pushf\n"
            "mov (%rsp), %rax\n"
            "and $0x40000, %rax\n"
            "test %rax, %rax\n"
            "jz skip_disable\n"
            "xorl $0x40000,(%rsp)\n"
            "skip_disable: popf\n");

    print_fault(info->si_addr, &ucontext->uc_mcontext);

    void *backtrace_array[MAX_BACKTRACE_DEPTH];
    size_t backtrace_size = backtrace(backtrace_array, MAX_BACKTRACE_DEPTH);
    /* print directly to avoid malloc and ignore the first 2 which are related
    to the signal handling */
    fprintf(stderr, "Error Callstack:\n");
    backtrace_symbols_fd(&backtrace_array[2], backtrace_size-2, fileno(stderr));

    dump_registers(&ucontext->uc_mcontext);
    dump_stack(&ucontext->uc_mcontext);
    if (crash_callback)
    {
        crash_callback(signum, ucontext);
    }
    else
        exit(-1);
}
