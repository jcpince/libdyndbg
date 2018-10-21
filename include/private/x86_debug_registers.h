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

#define X86_DEBUG_STATUS_REG    6
#define X86_DEBUG_CONTROL_REG   7
#define X86_DEBUG_MAX_REG       8

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
    uint8_t         le:1;
    uint8_t         ge:1;
    uint8_t         res0:1; /* bit 10 */
    uint8_t         rtm:1;
    uint8_t         res1:1;
    uint8_t         gd:1;
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
    uint8_t         b0:1;
    uint8_t         b1:1;
    uint8_t         b2:1;
    uint8_t         b3:1;
    uint16_t        res0:9;
    uint8_t         bd:1;
    uint8_t         bs:1;
    uint8_t         bt:1;
    uint8_t         rtm:1;
    uint64_t        upper1:47;
} x86_breakpoint_status_t;

static inline uint64_t read_drx(pid_t pid, uint8_t x)
{
    assert(x < X86_DEBUG_MAX_REG);
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

static inline int write_drx(pid_t pid, uint8_t x, uint64_t val)
{
    assert(x < X86_DEBUG_MAX_REG);
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

#endif /* __X86_DEBUG_REGISTERS__ */
