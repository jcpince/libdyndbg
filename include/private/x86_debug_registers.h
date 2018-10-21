#ifndef __X86_DEBUG_REGISTERS__
#define __X86_DEBUG_REGISTERS__

#include <private/dyndbg_monitor.h>
#include <dyndbg/dyndbg_us.h>

#include <sys/ptrace.h>
#include <sys/user.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#define X86_DBG_STATUS_VALID(x)     (x.res0 == 0)
#define X86_DBG_CONTROL_VALID(x)    (x.res0 == 0)

typedef enum
{
    X86_HW_BREAKPOINT_0 = 0,
    X86_HW_BREAKPOINT_1 = 1,
    X86_HW_BREAKPOINT_2 = 2,
    X86_HW_BREAKPOINT_3 = 3,

    X86_HW_BREAKPOINT_STATUS = 6,
    X86_HW_BREAKPOINT_CONTROL = 7,
    X86_HW_BREAKPOINT_MAX_REG = 8,
} x86_breakpoint_register_t;

typedef struct __attribute__((packed))
{
    uint8_t         l0:1; /* local dr0 breakpoint address enabled */
    uint8_t         g0:1; /* global dr0 breakpoint address enabled */
    uint8_t         l1:1;
    uint8_t         g1:1;
    uint8_t         l2:1;
    uint8_t         g2:1;
    uint8_t         l3:1;
    uint8_t         g3:1;
    uint8_t         le:1; /* local exact breakpt detected -- recommended to 1*/
    uint8_t         ge:1; /* global exact breakpt detected -- recommended to 1*/
    uint8_t         res0:1; /* bit 10 */
    uint8_t         rtm:1; /* restricted transactional memory enable */
    uint8_t         res1:1;
    uint8_t         gd:1; /* general detect to protect DR registers */
    uint8_t         res2:2;
    ddbg_btype_t    rw0:2;  /* dr0 breakpoint type */
    ddbg_bsize_t    len0:2; /* dr0 breakpoint length */
    ddbg_btype_t    rw1:2;  /* dr0 breakpoint type -- bits 20-21 */
    ddbg_bsize_t    len1:2;
    ddbg_btype_t    rw2:2;
    ddbg_bsize_t    len2:2;
    ddbg_btype_t    rw3:2;
    ddbg_bsize_t    len3:2; /* bits 30-31 */
    uint32_t        upper0;
} x86_breakpoint_control_t;

typedef struct __attribute__((packed))
{
    uint8_t         b0:1; /*debug breakpoint dr0 triggered */
    uint8_t         b1:1;
    uint8_t         b2:1;
    uint8_t         b3:1;
    uint16_t        res0:9;
    uint8_t         bd:1;  /* debug register access detected */
    uint8_t         bs:1;  /* single step debug exception */
    uint8_t         bt:1;  /* task switch debug exception */
    uint8_t         rtm:1; /* restricted memory region access detected */
    uint64_t        upper1:47;
} x86_breakpoint_status_t;

static inline uint64_t x86_read_drx(pid_t pid, x86_breakpoint_register_t x)
{
    assert(x < X86_HW_BREAKPOINT_MAX_REG);
    errno = 0;
    uint64_t drx = ptrace(PTRACE_PEEKUSER, pid,
            offsetof(struct user, u_debugreg[x]), 0);
    int errno_ = errno;
    if (errno)
        error_print("ptrace( PTRACE_PEEKUSER, %d, offsetof(struct user, "\
            "u_debugreg[%d])) returned %lx, errno %d -- %s\n",
            pid, x, drx, errno_, strerror(errno_));
    errno = errno_;
    return drx;
}

static inline int x86_write_drx(pid_t pid, x86_breakpoint_register_t x,
    uint64_t val)
{
    assert(x < X86_HW_BREAKPOINT_MAX_REG);
    errno = 0;
    int rc = ptrace( PTRACE_POKEUSER, pid,
            offsetof(struct user, u_debugreg[x]), val);
    int errno_ = errno;
    if (errno)
        error_print("ptrace( PTRACE_POKEUSER, %d, offsetof(struct user, "\
            "u_debugreg[%d], 0x%lx)) returned %d, errno %d -- %s\n",
            pid, x, val, rc, errno_, strerror(errno_));
    errno = errno_;
    return rc;
}

static inline x86_breakpoint_status_t x86_read_dr_status(pid_t pid)
{
    uint64_t status = x86_read_drx(pid, X86_HW_BREAKPOINT_STATUS);
    return *((x86_breakpoint_status_t *)((void *)&status));
}

static inline int x86_write_dr_status(pid_t pid, x86_breakpoint_status_t stat)
{
    return x86_write_drx(pid, X86_HW_BREAKPOINT_STATUS, *((uint64_t*)&stat));
}

static inline x86_breakpoint_control_t x86_read_dr_control(pid_t pid)
{
    uint64_t ctrl = x86_read_drx(pid, X86_HW_BREAKPOINT_CONTROL);
    return *((x86_breakpoint_control_t *)((void *)&ctrl));
}

static inline int x86_write_dr_control(pid_t pid, x86_breakpoint_control_t ctrl)
{
    return x86_write_drx(pid, X86_HW_BREAKPOINT_CONTROL, *((uint64_t*)&ctrl));
}

#endif /* __X86_DEBUG_REGISTERS__ */
