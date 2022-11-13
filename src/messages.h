#ifndef __MESSAGES_H
#define __MESSAGES_H

#include <exec/ports.h>

struct VC4Msg {
    struct Message  msg;
    ULONG           cmd;
    union
    {
        struct {
            UBYTE val;
        } SetPhase;

        struct {
            UBYTE val;
        } SetScaler;

        struct {
            UBYTE val;
        } GetPhase;

        struct {
            UBYTE val;
        } GetScaler;

        struct {
            UBYTE kernel;
            double b;
            double c;
        } SetKernel;

        struct {
            UBYTE kernel;
            double b;
            double c;
            WORD kernel_val[16];
        } GetKernel;
    };
};

enum {
    VCMD_SET_PHASE,
    VCMD_GET_PHASE,
    VCMD_SET_SCALER,
    VCMD_GET_SCALER,
    VCMD_SET_KERNEL,
    VCMD_GET_KERNEL
};

#endif /* __MESSAGES_H */
