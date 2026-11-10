/*
    Vertical blank interrupt of the pixelvalve of HDMI0 for Picasso96.

    The pixelvalve raises it at the start of the vertical front porch, i.e. when the beam leaves the picture.
    How it reaches the 68k depends on the chip:

      VC6 (BCM2711)       a SPI of the GIC-400, delivered to a 68k handler by gic400.library (which Emu68 has in
                          its ROM).
      VC4 (BCM2837)       a GPU interrupt of the ARM interrupt controller, delivered to a 68k handler by
                          arm-intc.library (a disk library, in LIBS:). It opens only where that controller is
                          the one of the device tree: Pi 3 and Pi Zero 2.

    Without the library of its family the driver works on, without the interrupt.

    The handler acknowledges it and causes the soft interrupt of rtg.library, which wakes the tasks waiting in
    WaitBOVP() / WaitTOF() and serves the double buffering.

    The pixelvalve only raises it while somebody needs it: rtg.library calls SetInterrupt() when a task starts to
    wait, and the handler switches it off again when nobody is left, so that there is no cost when idle.
*/

#include <exec/types.h>
#include <exec/interrupts.h>
#include <exec/nodes.h>
#include <hardware/intbits.h>

#include <proto/exec.h>
#include <proto/devicetree.h>
#include <proto/gic400.h>
#include <proto/arm-intc.h>
#include <libraries/arm-intc.h>

#include <stdint.h>
#include <common/compiler.h>

#include "boardinfo.h"
#include "emu68-vc4.h"
#include "vblank.h"

/* Pixelvalve 2 drives HDMI0 on the BCM2711 and on the BCM2835/2837 */
#define PV2_NODE_VC6        "/soc/pixelvalve@7e20a000"
#define PV2_BASE_VC6        0xf220a000
#define PV2_NODE_VC4        "/soc/pixelvalve@7e807000"
#define PV2_BASE_VC4        0xf2807000

#define PV_INTEN            0x24
#define PV_INTSTAT          0x28

#define PV_INT_VFP_START    (1UL << 7)      /* start of the vertical front porch: the picture is over */

/*
    gic400.library 1.3 is the first one which finds the GIC through the interrupt-parent of the device tree.
    AddIntServerEx() and RemIntServerEx() have the same LVOs and registers since 1.0 and return 0 on success.
*/
#define GIC400_NAME         "gic400.library"
#define GIC400_MIN_VERSION  1
#define GIC400_MIN_REVISION 3

/* Reads of the pixelvalve while waiting for the first interrupt: about 20 ms, a few frames */
#define VBLANK_TEST_READS   1000000

static inline ULONG PV_Read(struct VC4Base *VC4Base, ULONG reg)
{
    return LE32(*(volatile ULONG *)(VC4Base->vc4_VBlankPV + reg));
}

static inline void PV_Write(struct VC4Base *VC4Base, ULONG reg, ULONG value)
{
    wr32le((volatile uint32_t *)(VC4Base->vc4_VBlankPV + reg), value);
}

/* No vertical blank raised nor pending: the state the pixelvalve must be in before its line is enabled */
static void VBlank_Quiet(struct VC4Base *VC4Base)
{
    PV_Write(VC4Base, PV_INTEN, 0);
    PV_Write(VC4Base, PV_INTSTAT, PV_INT_VFP_START);
}

/*
    Called at the interrupt of the pixelvalve, with the BoardInfo in a1 (the is_Data of the interrupt), in
    interrupt context: nothing which can wait. On the VC6 by gic400.library, on the VC4 by arm-intc.library
    through the stub below.
*/
static ULONG VBlank_Interrupt(REGARG(struct BoardInfo *bi, "a1"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)bi->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    BOOL waiting;

    if ((PV_Read(VC4Base, PV_INTSTAT) & PV_INT_VFP_START) == 0)
        return 0;

    PV_Write(VC4Base, PV_INTSTAT, PV_INT_VFP_START);    /* write 1 to clear */
    VC4Base->vc4_VBlankCount++;

    /* Nobody waits and nothing is double buffered: switch the interrupt off until SetInterrupt() is called or a
       position of the sprite is queued. This comes before the sprite: a word queued after it switches the
       interrupt on again, a word queued before it is written below. */
    waiting = !((bi->WaitQ.mlh_Head == NULL || bi->WaitQ.mlh_Head->mln_Succ == NULL) && bi->DoubleBufferList == NULL);
    if (!waiting)
        PV_Write(VC4Base, PV_INTEN, 0);

    /* The flag is cleared before the word is read: a word queued meanwhile sets it again */
    if (VC4Base->vc4_SpritePending)
    {
        VC4Base->vc4_SpritePending = FALSE;

        if (VC4Base->vc4_MouseCoord != NULL)
            wr32le(&VC4Base->vc4_MouseCoord[0], VC4Base->vc4_SpriteWord);
    }

    if (waiting)
        Cause(&bi->SoftInterrupt);

    return 1;
}

/*
    The server of arm-intc.library on the VC4. The library tests the Z flag on return, which the epilogue of C
    code may set again: claimed is d0 non zero with Z clear, not claimed is d0 zero with Z set.
*/
void VBlank_AICServer(void);
asm(
    "       .text                       \n"
    "       .even                       \n"
    "_VBlank_AICServer:                 \n"
    "       jsr     _VBlank_Interrupt   \n"     /* is_Data is in a1 already */
    "       tst.l   d0                  \n"
    "       rts                         \n"
);

/* Switches the source on. The status is cleared only when the source was off: when it is on, a status already
   set is a vertical blank about to be served, which must not be lost. */
static void VBlank_Arm(struct VC4Base *VC4Base)
{
    if ((PV_Read(VC4Base, PV_INTEN) & PV_INT_VFP_START) == 0)
    {
        PV_Write(VC4Base, PV_INTSTAT, PV_INT_VFP_START);
        PV_Write(VC4Base, PV_INTEN, PV_INT_VFP_START);
    }
}

/* rtg.library enables the interrupt when a task starts to wait and disables it at the screen switches.
   A position of the sprite which waits keeps it on until the next vertical blank. Returns the previous state. */
static BOOL VBlank_SetInterrupt(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL enable, "d0"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    BOOL previous = (PV_Read(VC4Base, PV_INTEN) & PV_INT_VFP_START) != 0;

    if (0)
    {
        bug("[VC] SetInterrupt(%ld)\n", (ULONG)enable);
    }

    if (enable)
        VBlank_Arm(VC4Base);
    else if (!VC4Base->vc4_SpritePending)
        PV_Write(VC4Base, PV_INTEN, 0);

    return previous;
}

void VBlank_WriteSprite(struct VC4Base *VC4Base, ULONG word)
{
    if (VC4Base->vc4_MouseCoord == NULL)
        return;

    if (VC4Base->vc4_VBlankInterrupt == NULL)
    {
        wr32le(&VC4Base->vc4_MouseCoord[0], word);
        return;
    }

    /* The word first, then the flag, then the source: whatever the handler is doing, it sees the flag at its
       next vertical blank at the latest */
    VC4Base->vc4_SpriteWord = word;
    VC4Base->vc4_SpritePending = TRUE;
    VBlank_Arm(VC4Base);
}

/*
    VC6: the interrupt of the pixelvalve, from its "interrupts" property: type, number, flags. A SPI (type 0) has
    the GIC id number + 32. 0 when it cannot be found.
*/
static ULONG VBlank_FindIrqVC6(struct VC4Base *VC4Base)
{
    APTR DeviceTreeBase = VC4Base->vc4_DeviceTreeBase;
    ULONG irq = 0;
    APTR key = DT_OpenKey(PV2_NODE_VC6);

    if (key != NULL)
    {
        APTR prop = DT_FindProperty(key, "interrupts");

        if (prop != NULL && DT_GetPropLen(prop) >= 3 * sizeof(ULONG))
        {
            const ULONG *cells = DT_GetPropValue(prop);

            if (cells[0] == 0)
                irq = cells[1] + 32;
        }

        DT_CloseKey(key);
    }

    return irq;
}

/* VC6: registers the handler with gic400.library. Returns TRUE when it is in place. */
static BOOL VBlank_AddServerVC6(struct VC4Base *VC4Base, struct BoardInfo *bi)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Library *GIC400_Base;
    ULONG irq;

    irq = VBlank_FindIrqVC6(VC4Base);
    if (irq == 0)
    {
        bug("[VC] VBlank: no interrupt in the device tree\n");
        return FALSE;
    }

    GIC400_Base = OpenLibrary((CONST_STRPTR)GIC400_NAME, 0);
    if (GIC400_Base == NULL)
    {
        bug("[VC] VBlank: no %s\n", (ULONG)GIC400_NAME);
        return FALSE;
    }

    if (GIC400_Base->lib_Version < GIC400_MIN_VERSION ||
        (GIC400_Base->lib_Version == GIC400_MIN_VERSION && GIC400_Base->lib_Revision < GIC400_MIN_REVISION))
    {
        bug("[VC] VBlank: %s %ld.%ld is too old\n", (ULONG)GIC400_NAME,
            (ULONG)GIC400_Base->lib_Version, (ULONG)GIC400_Base->lib_Revision);
        CloseLibrary(GIC400_Base);
        return FALSE;
    }

    bi->HardInterrupt.is_Code = (void (*)())VBlank_Interrupt;

    /* Level triggered, priority 0 */
    VBlank_Quiet(VC4Base);
    if (AddIntServerEx(irq, 0, FALSE, &bi->HardInterrupt) != 0)
    {
        bug("[VC] VBlank: cannot register interrupt %ld\n", irq);
        CloseLibrary(GIC400_Base);
        return FALSE;
    }

    /* gic400.library stays open for as long as the handler is registered */
    VC4Base->vc4_GIC400Base = GIC400_Base;
    VC4Base->vc4_VBlankIrq = irq;

    return TRUE;
}

/* VC6: undoes VBlank_AddServerVC6(), in the reverse order */
static void VBlank_RemServerVC6(struct VC4Base *VC4Base, struct Interrupt *interrupt)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Library *GIC400_Base = VC4Base->vc4_GIC400Base;

    RemIntServerEx(VC4Base->vc4_VBlankIrq, interrupt);
    CloseLibrary(GIC400_Base);
    VC4Base->vc4_GIC400Base = NULL;
}

/* VC4: registers the handler with arm-intc.library. Returns TRUE when it is in place. */
static BOOL VBlank_AddServerVC4(struct VC4Base *VC4Base, struct BoardInfo *bi)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    APTR DeviceTreeBase = VC4Base->vc4_DeviceTreeBase;
    struct Library *ArmIntcBase;
    APTR key;
    LONG irq = AICERR_NODT;

    /* It opens only on the controller it knows: on any other VC4 there is no interrupt */
    ArmIntcBase = OpenLibrary((CONST_STRPTR)ARMINTCNAME, 0);
    if (ArmIntcBase == NULL)
    {
        bug("[VC] VBlank: no %s\n", (ULONG)ARMINTCNAME);
        return FALSE;
    }

    key = DT_OpenKey(PV2_NODE_VC4);
    if (key != NULL)
    {
        irq = AIC_GetInterrupt(key, 0);
        DT_CloseKey(key);
    }

    if (irq < 0)
    {
        bug("[VC] VBlank: no interrupt in the device tree (%ld)\n", irq);
        CloseLibrary(ArmIntcBase);
        return FALSE;
    }

    bi->HardInterrupt.is_Node.ln_Type = NT_INTERRUPT;
    bi->HardInterrupt.is_Node.ln_Pri = 0;
    bi->HardInterrupt.is_Node.ln_Name = bi->BoardName;
    bi->HardInterrupt.is_Code = (void (*)())VBlank_AICServer;

    /* The first server of a line enables it at once: the pixelvalve must be quiet first */
    VBlank_Quiet(VC4Base);
    if (AIC_AddIntServer(irq, &bi->HardInterrupt) != AICERR_OK)
    {
        bug("[VC] VBlank: cannot register interrupt %ld\n", irq);
        CloseLibrary(ArmIntcBase);
        return FALSE;
    }

    /* arm-intc.library stays open for as long as the handler is registered */
    VC4Base->vc4_ArmIntcBase = ArmIntcBase;
    VC4Base->vc4_VBlankIrq = irq;

    return TRUE;
}

/* VC4: undoes VBlank_AddServerVC4(), in the reverse order */
static void VBlank_RemServerVC4(struct VC4Base *VC4Base, struct Interrupt *interrupt)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Library *ArmIntcBase = VC4Base->vc4_ArmIntcBase;

    AIC_RemIntServer(VC4Base->vc4_VBlankIrq, interrupt);
    CloseLibrary(ArmIntcBase);
    VC4Base->vc4_ArmIntcBase = NULL;
}

static void VBlank_RemServer(struct VC4Base *VC4Base, struct Interrupt *interrupt)
{
    if (VC4Base->vc4_VideoCore6)
        VBlank_RemServerVC6(VC4Base, interrupt);
    else
        VBlank_RemServerVC4(VC4Base, interrupt);
}

BOOL VBlank_Init(struct BoardInfo *bi)
{
    struct VC4Base *VC4Base = (struct VC4Base *)bi->CardBase;
    BOOL added;

    VC4Base->vc4_VBlankPV = VC4Base->vc4_VideoCore6 ? PV2_BASE_VC6 : PV2_BASE_VC4;

    /* An OS restart resets neither the interrupt controller nor the pixelvalve: PV_INTEN may still be armed by
       the previous session. Each family quiets the pixelvalve before it registers the handler. */
    bi->HardInterrupt.is_Data = bi;

    if (VC4Base->vc4_VideoCore6)
        added = VBlank_AddServerVC6(VC4Base, bi);
    else
        added = VBlank_AddServerVC4(VC4Base, bi);

    if (!added)
        return FALSE;

    /* Being registered does not mean that the interrupt arrives: let the pixelvalve raise one and see it come.
       If nothing comes, undo everything and stay on the polling of WaitVerticalSync(). */
    VC4Base->vc4_VBlankCount = 0;
    PV_Write(VC4Base, PV_INTSTAT, PV_INT_VFP_START);
    PV_Write(VC4Base, PV_INTEN, PV_INT_VFP_START);

    for (ULONG n = 0; n < VBLANK_TEST_READS && VC4Base->vc4_VBlankCount == 0; n++)
        (void)PV_Read(VC4Base, PV_INTSTAT);

    PV_Write(VC4Base, PV_INTEN, 0);

    if (VC4Base->vc4_VBlankCount == 0)
    {
        bug("[VC] VBlank: no interrupt %ld, staying on polling\n", VC4Base->vc4_VBlankIrq);
        VBlank_RemServer(VC4Base, &bi->HardInterrupt);
        return FALSE;
    }

    /* The last step: tell rtg.library */
    VC4Base->vc4_VBlankInterrupt = &bi->HardInterrupt;

    bi->SetInterrupt = (void *)VBlank_SetInterrupt;
    bi->Flags |= BIF_VBLANKINTERRUPT;

    /* VC6: GIC id. VC4: line of arm-intc.library, bank * 32 + bit */
    bug("[VC] VBlank: interrupt %ld of the pixelvalve of HDMI0\n", VC4Base->vc4_VBlankIrq);

    return TRUE;
}

void VBlank_Exit(struct VC4Base *VC4Base)
{
    if (VC4Base->vc4_VBlankInterrupt == NULL)
        return;

    PV_Write(VC4Base, PV_INTEN, 0);
    VBlank_RemServer(VC4Base, VC4Base->vc4_VBlankInterrupt);

    VC4Base->vc4_VBlankInterrupt = NULL;
}
