#ifndef __X86_DEBUG_REGISTERS__
#define __X86_DEBUG_REGISTERS__

#include "dyndebug_us.h"

typedef struct __attribute__((packed))
{
    uint8_t         l0:1;
    uint8_t         g0:1;
    uint8_t         l1:1;
    uint8_t         g1:1;
    uint8_t         l2:1;
    uint8_t         g2:1;
    uint8_t         l3:1;
    uint8_t         g3:1;
    uint8_t         le:1;
    uint8_t         ge:1;
    uint8_t         res0:1; // 10
    uint8_t         rtm:1;
    uint8_t         res1:1;
    uint8_t         gd:1;
    uint8_t         res2:2;
    ddbg_btype_t    rw0:2;
    ddbg_bsize_t    len0:2;
    ddbg_btype_t    rw1:2;  // 20-21
    ddbg_bsize_t    len1:2;
    ddbg_btype_t    rw2:2;
    ddbg_bsize_t    len2:2;
    ddbg_btype_t    rw3:2;
    ddbg_bsize_t    len3:2; // 30-31
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

#endif /* __X86_DEBUG_REGISTERS__ */
