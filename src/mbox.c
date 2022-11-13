/*
    Copyright © 2021 Michal Schulz <michal.schulz@gmx.de>
    https://github.com/michalsc

    This Source Code Form is subject to the terms of the
    Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed
    with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/


#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <exec/errors.h>

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/devicetree.h>

#include <libraries/configregs.h>
#include <libraries/configvars.h>

#include <stdint.h>

#include "emu68-vc4.h"



/* status register flags */

#define MBOX_TX_FULL (1UL << 31)
#define MBOX_RX_EMPTY (1UL << 30)
#define MBOX_CHANMASK 0xF

/* VideoCore tags used. */

#define VCTAG_GET_ARM_MEMORY     0x00010005
#define VCTAG_GET_CLOCK_RATE     0x00030002

static uint32_t mbox_recv(uint32_t channel, struct VC4Base * VC4Base)
{
	volatile uint32_t *mbox_read = (uint32_t*)(VC4Base->vc4_MailBox);
	volatile uint32_t *mbox_status = (uint32_t*)((uintptr_t)VC4Base->vc4_MailBox + 0x18);
	uint32_t response, status;

	do
	{
		do
		{
			status = LE32(*mbox_status);
			asm volatile("nop");
		}
		while (status & MBOX_RX_EMPTY);

		asm volatile("nop");
		response = LE32(*mbox_read);
		asm volatile("nop");
	}
	while ((response & MBOX_CHANMASK) != channel);

	return (response & ~MBOX_CHANMASK);
}

static void mbox_send(uint32_t channel, uint32_t data, struct VC4Base * VC4Base)
{
	volatile uint32_t *mbox_write = (uint32_t*)((uintptr_t)VC4Base->vc4_MailBox + 0x20);
	volatile uint32_t *mbox_status = (uint32_t*)((uintptr_t)VC4Base->vc4_MailBox + 0x18);
	uint32_t status;

	data &= ~MBOX_CHANMASK;
	data |= channel & MBOX_CHANMASK;

	do
	{
		status = LE32(*mbox_status);
		asm volatile("nop");
	}
	while (status & MBOX_TX_FULL);

	asm volatile("nop");
	*mbox_write = LE32(data);
}

void get_vc_memory(void **base, uint32_t *size, struct VC4Base * VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    ULONG *FBReq = VC4Base->vc4_Request;
    ULONG len = 8*4;

    FBReq[0] = LE32(4*8);
    FBReq[1] = 0;
    FBReq[2] = LE32(0x00010006);
    FBReq[3] = LE32(8);
    FBReq[4] = 0;
    FBReq[5] = 0;
    FBReq[6] = 0;
    FBReq[7] = 0;

    CacheClearE(FBReq, len, CACRF_ClearD);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);

    if (base) {
        *base = (void *)(intptr_t)LE32(FBReq[5]);
    }

    if (size) {
        *size = LE32(FBReq[6]);
    }
}

struct Size get_display_size_(struct VC4Base * VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    ULONG *FBReq = VC4Base->vc4_Request;
    ULONG len = 8*4;

    struct Size sz;

    FBReq[0] = LE32(4*8);
    FBReq[1] = 0;
    FBReq[2] = LE32(0x00040003);
    FBReq[3] = LE32(8);
    FBReq[4] = 0;
    FBReq[5] = 0;
    FBReq[6] = 0;
    FBReq[7] = 0;

    CacheClearE(FBReq, len, CACRF_ClearD);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);

    sz.width = LE32(FBReq[5]);
    sz.height = LE32(FBReq[6]);

    return sz;
}

struct Size get_display_size(struct VC4Base *VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    int c = 1;
    ULONG *FBReq = VC4Base->vc4_Request;
    struct Size dimension;

    FBReq[c++] = 0;
    FBReq[c++] = LE32(0x40003);
    FBReq[c++] = LE32(8);
    FBReq[c++] = 0;
    FBReq[c++] = LE32(0);
    FBReq[c++] = LE32(0);
    FBReq[c++] = 0;

    FBReq[0] = LE32(c << 2);

    CacheClearE(FBReq, c*4, CACRF_ClearD);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);

    dimension.width = LE32(FBReq[5]);
    dimension.height = LE32(FBReq[6]);

    return dimension;
}

void init_display(struct Size dimensions, uint8_t depth, void **framebuffer, uint32_t *pitch, struct VC4Base * VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    ULONG *FBReq = VC4Base->vc4_Request;

    int c = 1;
    int pos_buffer_base = 0;
    int pos_buffer_pitch = 0;

    FBReq[c++] = 0;                 // Request
    FBReq[c++] = LE32(0x48003);     // SET_RESOLUTION
    FBReq[c++] = LE32(8);
    FBReq[c++] = 0;
    FBReq[c++] = LE32(dimensions.width);
    FBReq[c++] = LE32(dimensions.height);

    FBReq[c++] = LE32(0x48004);          // Virtual resolution: duplicate physical size...
    FBReq[c++] = LE32(8);
    FBReq[c++] = 0;
    FBReq[c++] = LE32(dimensions.width);
    FBReq[c++] = LE32(dimensions.height);

    FBReq[c++] = LE32(0x48005);   // Set depth
    FBReq[c++] = LE32(4);
    FBReq[c++] = LE32(0);
    FBReq[c++] = LE32(depth);

    FBReq[c++] = LE32(0x40001); // Allocate buffer
    FBReq[c++] = LE32(8);
    FBReq[c++] = LE32(0);
    pos_buffer_base = c;
    FBReq[c++] = LE32(64);
    FBReq[c++] = LE32(0);

    FBReq[c++] = LE32(0x40008); // Get pitch
    FBReq[c++] = LE32(4);
    FBReq[c++] = LE32(0);
    pos_buffer_pitch = c;
    FBReq[c++] = LE32(0);

    FBReq[c++] = 0;

    FBReq[0] = LE32(c << 2);

    CacheClearE(FBReq, c*4, CACRF_ClearD);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);

    uint32_t _base = LE32(FBReq[pos_buffer_base]);
    uint32_t _pitch = LE32(FBReq[pos_buffer_pitch]);

    if (framebuffer)
        *framebuffer = (void*)(intptr_t)_base;

    if (pitch)
        *pitch = _pitch;
}

int blank_screen(int blank, struct VC4Base *VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;

    ULONG *FBReq = VC4Base->vc4_Request;
    ULONG len = 7*4;

    FBReq[0] = LE32(4*7);
    FBReq[1] = 0;
    FBReq[2] = LE32(0x00040003);
    FBReq[3] = LE32(4);
    FBReq[4] = 0;
    FBReq[5] = blank ? 1 : 0;
    FBReq[6] = 0;

    CachePreDMA(FBReq, &len, 0);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);
    CachePostDMA(FBReq, &len, 0);

    return LE32(FBReq[5]) & 1;
}

static void putch(UBYTE data asm("d0"), APTR ignore asm("a3"))
{
    *(UBYTE*)0xdeadbeef = data;
}

void release_framebuffer(struct VC4Base *VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    ULONG *FBReq = VC4Base->vc4_Request;

    /* Release framebuffer */
    FBReq[0] = LE32(4*6);
    FBReq[1] = 0;
    FBReq[2] = LE32(0x00048001);
    FBReq[3] = LE32(0);
    FBReq[4] = 0;
    FBReq[5] = 0;

    CacheClearE(FBReq, 4*6, CACRF_ClearD);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);
}

uint32_t upload_code(const void * code, uint32_t code_size, struct VC4Base *VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    ULONG *FBReq = VC4Base->vc4_Request;
    ULONG handle;
    ULONG phys_addr;
    UBYTE *ptr;

    /* Allocate buffer for the code on VC4 */
    FBReq[0] = LE32(4*9);
    FBReq[1] = 0;
    FBReq[2] = LE32(0x3000c);
    FBReq[3] = LE32(12);
    FBReq[4] = 0;
    FBReq[5] = LE32(code_size);
    FBReq[6] = LE32(4);   // 4 byte align
    FBReq[7] = LE32((3 << 2) | (1 << 6));   // COHERENT | DIRECT | HINT_PERMALOCK
    FBReq[8] = 0;

    CacheClearE(FBReq, 4*9, CACRF_ClearD);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);

    handle = LE32(FBReq[5]);

    /* Lock the block so that it remains alive all the time */
    FBReq[0] = LE32(4*7);
    FBReq[1] = 0;
    FBReq[2] = LE32(0x0003000d);
    FBReq[3] = LE32(4);
    FBReq[4] = 0;
    FBReq[5] = LE32(handle);  // 32 bytes
    FBReq[6] = 0;

    CacheClearE(FBReq, 4*9, CACRF_ClearD);
    mbox_send(8, (ULONG)FBReq, VC4Base);
    mbox_recv(8, VC4Base);

    /* Get physical address. This is in VPU's view! */
    phys_addr = LE32(FBReq[5]);

    /* Convert address to CPU view, upload code there */
    ptr = (UBYTE *)(phys_addr & 0x3fffffff);
    for (int i=0; i < code_size; i++) {
        ptr[i] = ((UBYTE*)code)[i];
    }

    /* Clear caches to make sure code is in VPU's accessible memory */
    CacheClearE(ptr, code_size, CACRF_ClearD);

    /* Return back physical address */
    return phys_addr;
}
