#
#   Copyright © 2021 Michal Schulz <michal.schulz@gmx.de>
#   https://github.com/michalsc
#
#   This Source Code Form is subject to the terms of the
#   Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
#   with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
#


# Copy block of memory using VPU - This function does not support overlap!
# Arguments:
#   r0 : start address of surce
#   r1 : start address of destination
#   r2 : block length in bytes


        mov r3, 64
        cmp r2, 4096
        bcs smaller_than_4K
loop:
        vld HY(0++, 0), (r0+=r3) REP 64
        add r0, 4096
        sub r2, 4096
        cmp r2, 4096
        vst HY(0++, 0), (r1+=r3) REP 64
        add r1, 4096
        bcc loop

smaller_than_4K:
        cmp r2, 1024
        bcs smaller_than_1K

loop2:  vld HY(0++, 0), (r0+=r3) REP 16
        add r0, 1024
        sub r2, 1024
        cmp r2, 1024
        vst HY(0++, 0), (r1+=r3) REP 16
        add r1, 1024
        bcc loop2

smaller_than_1K:
        cmp r2, 256
        bcs smaller_than_256

loop3:  vld HY(0++, 0), (r0+=r3) REP 4
        add r0, 256
        sub r2, 256
        cmp r2, 256
        vst HY(0++, 0), (r1+=r3) REP 4
        add r1, 256
        bcc loop3

smaller_than_256:
        cmp r2, 64
        bcs smaller_than_64

loop4:  vld HY(0, 0), (r0)
        add r0, 64
        sub r2, 64
        cmp r2, 64
        vst HY(0, 0), (r1)
        add r1, 64
        bcc loop4

smaller_than_64:

        cmp r2, 4
        bcs smaller_than_4

loop5:  ld r4, (r0)
        st r4, (r1)
        add r0, 4
        add r1, 4
        sub r2, 4
        cmp r2, 4
        bcc loop5

smaller_than_4:
        cmp r2, 0
        beq exit
        ldb r4, (r0)
        stb r4, (r1)
        add r0, 1
        add r1, 1
        sub r2, 1
        b smaller_than_4
        
exit:   rts