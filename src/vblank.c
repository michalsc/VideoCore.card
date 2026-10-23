/*
    Vertical blank interrupt of the pixelvalve of HDMI0 for Picasso96.

    The interrupt is a SPI of the GIC-400, delivered to a 68k handler by gic400.library (which Emu68 has in its
    ROM). The pixelvalve raises it at the start of the vertical front porch, i.e. when the beam leaves the picture.
    The handler acknowledges it and causes the soft interrupt of rtg.library, which wakes the tasks waiting in
    WaitBOVP() / WaitTOF() and serves the double buffering.

    The pixelvalve only raises it while somebody needs it: rtg.library calls SetInterrupt() when a task starts to
    wait, and the handler switches it off again when nobody is left, so that there is no cost when idle.
*/

#include <exec/types.h>
#include <exec/interrupts.h>

#include <proto/exec.h>
#include <proto/devicetree.h>
#include <proto/gic400.h>

#include <stdint.h>
#include <common/compiler.h>

#include "boardinfo.h"
#include "emu68-vc4.h"
#include "vblank.h"

/* Pixelvalve 2 drives HDMI0 on the BCM2711 */
#define PV2_NODE            "/soc/pixelvalve@7e20a000"
#define PV2_BASE            0xf220a000

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

static inline ULONG PV_Read(ULONG reg)
{
    return LE32(*(volatile ULONG *)(PV2_BASE + reg));
}

static inline void PV_Write(ULONG reg, ULONG value)
{
    wr32le((volatile uint32_t *)(PV2_BASE + reg), value);
}

/* The GIC interrupt id of the pixelvalve, from its "interrupts" property: type, number, flags. A SPI (type 0)
   has the id number + 32. Returns 0 when it cannot be found. */
static ULONG VBlank_FindIrq(struct VC4Base *VC4Base)
{
    APTR DeviceTreeBase = VC4Base->vc4_DeviceTreeBase;
    ULONG irq = 0;
    APTR key = DT_OpenKey(PV2_NODE);

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

/*
    Called by gic400.library at the interrupt of the pixelvalve, with the BoardInfo in a1 (the is_Data of the
    interrupt), in interrupt context: nothing which can wait.
*/
static ULONG VBlank_Interrupt(REGARG(struct BoardInfo *bi, "a1"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)bi->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    BOOL waiting;

    if ((PV_Read(PV_INTSTAT) & PV_INT_VFP_START) == 0)
        return 0;

    PV_Write(PV_INTSTAT, PV_INT_VFP_START);     /* write 1 to clear */
    VC4Base->vc4_VBlankCount++;

    /* Nobody waits and nothing is double buffered: switch the interrupt off until SetInterrupt() is called or a
       position of the sprite is queued. This comes before the sprite: a word queued after it switches the
       interrupt on again, a word queued before it is written below. */
    waiting = !((bi->WaitQ.mlh_Head == NULL || bi->WaitQ.mlh_Head->mln_Succ == NULL) && bi->DoubleBufferList == NULL);
    if (!waiting)
        PV_Write(PV_INTEN, 0);

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

/* Switches the source on. The status is cleared only when the source was off: when it is on, a status already
   set is a vertical blank about to be served, which must not be lost. */
static void VBlank_Arm(void)
{
    if ((PV_Read(PV_INTEN) & PV_INT_VFP_START) == 0)
    {
        PV_Write(PV_INTSTAT, PV_INT_VFP_START);
        PV_Write(PV_INTEN, PV_INT_VFP_START);
    }
}

/* rtg.library enables the interrupt when a task starts to wait and disables it at the screen switches.
   A position of the sprite which waits keeps it on until the next vertical blank. Returns the previous state. */
static BOOL VBlank_SetInterrupt(REGARG(struct BoardInfo *b, "a0"), REGARG(BOOL enable, "d0"))
{
    struct VC4Base *VC4Base = (struct VC4Base *)b->CardBase;
    BOOL previous = (PV_Read(PV_INTEN) & PV_INT_VFP_START) != 0;

    if (0)
    {
        bug("[VC] SetInterrupt(%ld)\n", (ULONG)enable);
    }

    if (enable)
        VBlank_Arm();
    else if (!VC4Base->vc4_SpritePending)
        PV_Write(PV_INTEN, 0);

    return previous;
}

void VBlank_WriteSprite(struct VC4Base *VC4Base, ULONG word)
{
    if (VC4Base->vc4_MouseCoord == NULL)
        return;

    if (VC4Base->vc4_GIC400Base == NULL)
    {
        wr32le(&VC4Base->vc4_MouseCoord[0], word);
        return;
    }

    /* The word first, then the flag, then the source: whatever the handler is doing, it sees the flag at its
       next vertical blank at the latest */
    VC4Base->vc4_SpriteWord = word;
    VC4Base->vc4_SpritePending = TRUE;
    VBlank_Arm();
}

BOOL VBlank_Init(struct BoardInfo *bi)
{
    struct VC4Base *VC4Base = (struct VC4Base *)bi->CardBase;
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Library *GIC400_Base;
    ULONG irq;

    /* The GIC-400 is the one of the Pi 4 */
    if (!VC4Base->vc4_VideoCore6)
        return FALSE;

    irq = VBlank_FindIrq(VC4Base);
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

    /* An OS restart resets neither the GIC nor the pixelvalve: PV_INTEN may still be armed by the previous
       session. Start from scratch. */
    PV_Write(PV_INTEN, 0);
    PV_Write(PV_INTSTAT, PV_INT_VFP_START);

    bi->HardInterrupt.is_Data = bi;
    bi->HardInterrupt.is_Code = (void (*)())VBlank_Interrupt;

    /* Level triggered, priority 0. The pixelvalve raises nothing before SetInterrupt() */
    if (AddIntServerEx(irq, 0, FALSE, &bi->HardInterrupt) != 0)
    {
        bug("[VC] VBlank: cannot register interrupt %ld\n", irq);
        CloseLibrary(GIC400_Base);
        return FALSE;
    }

    /* Being registered does not mean that the interrupt arrives: let the pixelvalve raise one and see it come.
       If nothing comes, undo everything and stay on the polling of WaitVerticalSync(). */
    VC4Base->vc4_VBlankCount = 0;
    PV_Write(PV_INTSTAT, PV_INT_VFP_START);
    PV_Write(PV_INTEN, PV_INT_VFP_START);

    for (ULONG n = 0; n < VBLANK_TEST_READS && VC4Base->vc4_VBlankCount == 0; n++)
        (void)PV_Read(PV_INTSTAT);

    PV_Write(PV_INTEN, 0);

    if (VC4Base->vc4_VBlankCount == 0)
    {
        bug("[VC] VBlank: no interrupt %ld, staying on polling\n", irq);
        RemIntServerEx(irq, &bi->HardInterrupt);
        CloseLibrary(GIC400_Base);
        return FALSE;
    }

    /* The last step: tell rtg.library. gic400.library stays open for as long as the driver lives. */
    VC4Base->vc4_GIC400Base = GIC400_Base;
    VC4Base->vc4_VBlankIrq = irq;
    VC4Base->vc4_VBlankInterrupt = &bi->HardInterrupt;

    bi->SetInterrupt = (void *)VBlank_SetInterrupt;
    bi->Flags |= BIF_VBLANKINTERRUPT;

    bug("[VC] VBlank: interrupt %ld of the pixelvalve of HDMI0\n", irq);

    return TRUE;
}

void VBlank_Exit(struct VC4Base *VC4Base)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Library *GIC400_Base = VC4Base->vc4_GIC400Base;

    if (GIC400_Base == NULL)
        return;

    PV_Write(PV_INTEN, 0);
    RemIntServerEx(VC4Base->vc4_VBlankIrq, VC4Base->vc4_VBlankInterrupt);
    CloseLibrary(GIC400_Base);

    VC4Base->vc4_GIC400Base = NULL;
    VC4Base->vc4_VBlankInterrupt = NULL;
}
