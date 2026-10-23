/*
    Vertical blank interrupt of the pixelvalve of HDMI0 for Picasso96.

    The pixelvalve raises it at the start of the vertical front porch, i.e. when the beam leaves the picture.
    How it reaches the 68k depends on the chip:

      VC6 (BCM2711)       a SPI of the GIC-400, delivered to a 68k handler by gic400.library (which Emu68 has in
                          its ROM).
      VC4 (BCM2836/2837)  a GPU interrupt of the ARM interrupt controller. Emu68 turns any ARM interrupt into
                          INTB_EXTER, so the handler is a server of that chain and the interrupt is enabled in the
                          controller.

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
    ARM interrupt controller of the BCM2836/2837 (irq-bcm2835.c), whose GPU interrupts the local controller of
    the cores passes on to the ARM. One enable and one disable register per bank, write 1 to act.
*/
#define ARMCTRL_NODE        "/soc/interrupt-controller@7e00b200"
#define ARMCTRL_COMPATIBLE  "brcm,bcm2836-armctrl-ic"
#define ARMCTRL_BASE        0xf200b200
#define ARMCTRL_ENABLE_1    0x10            /* bank 1, bank 2 follows at +4 */
#define ARMCTRL_DISABLE_1   0x1c            /* bank 1, bank 2 follows at +4 */

/*
    gic400.library 1.3 is the first one which finds the GIC through the interrupt-parent of the device tree.
    AddIntServerEx() and RemIntServerEx() have the same LVOs and registers since 1.0 and return 0 on success.
*/
#define GIC400_NAME         "gic400.library"
#define GIC400_MIN_VERSION  1
#define GIC400_MIN_REVISION 3

/* Reads of the pixelvalve while waiting for the first interrupt: about 20 ms, a few frames */
#define VBLANK_TEST_READS   1000000

int _strcmp(const char *s1, const char *s2);

static inline ULONG PV_Read(struct VC4Base *VC4Base, ULONG reg)
{
    return LE32(*(volatile ULONG *)(VC4Base->vc4_VBlankPV + reg));
}

static inline void PV_Write(struct VC4Base *VC4Base, ULONG reg, ULONG value)
{
    wr32le((volatile uint32_t *)(VC4Base->vc4_VBlankPV + reg), value);
}

/* Acts on a GPU interrupt of the ARM controller: reg is ARMCTRL_ENABLE_1 or ARMCTRL_DISABLE_1, irq is
   bank << 5 | bit as VBlank_FindIrq() gives it */
static inline void ARMCtrl_Write(ULONG reg, ULONG irq)
{
    wr32le((volatile uint32_t *)(ARMCTRL_BASE + reg + ((irq >> 5) - 1) * 4), 1UL << (irq & 31));
}

/*
    The interrupt of the pixelvalve, from its "interrupts" property, 0 when it cannot be found.
      VC6: type, number, flags. A SPI (type 0) has the GIC id number + 32.
      VC4: bank, bit. Banks 1 and 2 are the GPU interrupts, returned as bank << 5 | bit.
*/
static ULONG VBlank_FindIrq(struct VC4Base *VC4Base)
{
    APTR DeviceTreeBase = VC4Base->vc4_DeviceTreeBase;
    ULONG irq = 0;
    APTR key = DT_OpenKey(VC4Base->vc4_VideoCore6 ? PV2_NODE_VC6 : PV2_NODE_VC4);

    if (key != NULL)
    {
        APTR prop = DT_FindProperty(key, "interrupts");

        if (VC4Base->vc4_VideoCore6)
        {
            if (prop != NULL && DT_GetPropLen(prop) >= 3 * sizeof(ULONG))
            {
                const ULONG *cells = DT_GetPropValue(prop);

                if (cells[0] == 0)
                    irq = cells[1] + 32;
            }
        }
        else
        {
            if (prop != NULL && DT_GetPropLen(prop) >= 2 * sizeof(ULONG))
            {
                const ULONG *cells = DT_GetPropValue(prop);

                if ((cells[0] == 1 || cells[0] == 2) && cells[1] < 32)
                    irq = (cells[0] << 5) | cells[1];
            }
        }

        DT_CloseKey(key);
    }

    return irq;
}

/*
    The VC4 interrupt needs the ARM controller of the BCM2836/2837, which Emu68 routes to the 68k through the
    local controller of the cores: Pi 3 and Pi Zero 2 (BCM2837 / RP3A0). The BCM2835 of the Pi Zero and Zero W
    has neither and is left on polling.
*/
static BOOL VBlank_HasARMCtrl(struct VC4Base *VC4Base)
{
    APTR DeviceTreeBase = VC4Base->vc4_DeviceTreeBase;
    BOOL found = FALSE;
    APTR key = DT_OpenKey(ARMCTRL_NODE);

    if (key != NULL)
    {
        const char *comp = DT_GetPropValue(DT_FindProperty(key, "compatible"));

        if (comp != NULL && _strcmp(ARMCTRL_COMPATIBLE, comp) == 0)
            found = TRUE;

        DT_CloseKey(key);
    }

    return found;
}

/*
    Called at the interrupt of the pixelvalve, with the BoardInfo in a1 (the is_Data of the interrupt), in
    interrupt context: nothing which can wait. On the VC6 by gic400.library, on the VC4 by the stub below.
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
    The server of INTB_EXTER on the VC4. exec tests the Z flag after each server of the chain, which C code does
    not set. The chain is shared with the CIA-B and the other ARM interrupts, so it always goes on: Z set.
*/
void VBlank_ExterServer(void);
asm(
    "       .text                       \n"
    "       .even                       \n"
    "_VBlank_ExterServer:               \n"
    "       jsr     _VBlank_Interrupt   \n"     /* is_Data is in a1 already */
    "       moveq   #0,d0               \n"
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

/* Registers the handler of the interrupt irq, see the top of the file. Returns TRUE when it is in place. */
static BOOL VBlank_AddServer(struct VC4Base *VC4Base, struct BoardInfo *bi, ULONG irq)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Library *GIC400_Base;

    if (!VC4Base->vc4_VideoCore6)
    {
        bi->HardInterrupt.is_Node.ln_Type = NT_INTERRUPT;
        bi->HardInterrupt.is_Node.ln_Pri = 0;
        bi->HardInterrupt.is_Node.ln_Name = bi->BoardName;
        bi->HardInterrupt.is_Code = (void (*)())VBlank_ExterServer;

        /* The server first, then the controller. The pixelvalve raises nothing before SetInterrupt() */
        AddIntServer(INTB_EXTER, &bi->HardInterrupt);
        ARMCtrl_Write(ARMCTRL_ENABLE_1, irq);

        return TRUE;
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

    /* Level triggered, priority 0. The pixelvalve raises nothing before SetInterrupt() */
    if (AddIntServerEx(irq, 0, FALSE, &bi->HardInterrupt) != 0)
    {
        bug("[VC] VBlank: cannot register interrupt %ld\n", irq);
        CloseLibrary(GIC400_Base);
        return FALSE;
    }

    /* gic400.library stays open for as long as the handler is registered */
    VC4Base->vc4_GIC400Base = GIC400_Base;

    return TRUE;
}

/* Undoes VBlank_AddServer(), in the reverse order */
static void VBlank_RemServer(struct VC4Base *VC4Base, ULONG irq, struct Interrupt *interrupt)
{
    struct ExecBase *SysBase = VC4Base->vc4_SysBase;
    struct Library *GIC400_Base = VC4Base->vc4_GIC400Base;

    if (GIC400_Base != NULL)
    {
        RemIntServerEx(irq, interrupt);
        CloseLibrary(GIC400_Base);
        VC4Base->vc4_GIC400Base = NULL;
    }
    else
    {
        ARMCtrl_Write(ARMCTRL_DISABLE_1, irq);
        RemIntServer(INTB_EXTER, interrupt);
    }
}

BOOL VBlank_Init(struct BoardInfo *bi)
{
    struct VC4Base *VC4Base = (struct VC4Base *)bi->CardBase;
    ULONG irq;

    if (VC4Base->vc4_VideoCore6)
        VC4Base->vc4_VBlankPV = PV2_BASE_VC6;
    else if (VBlank_HasARMCtrl(VC4Base))
        VC4Base->vc4_VBlankPV = PV2_BASE_VC4;
    else
    {
        bug("[VC] VBlank: no ARM interrupt controller of the BCM2836/2837\n");
        return FALSE;
    }

    irq = VBlank_FindIrq(VC4Base);
    if (irq == 0)
    {
        bug("[VC] VBlank: no interrupt in the device tree\n");
        return FALSE;
    }

    /* An OS restart resets neither the interrupt controller nor the pixelvalve: PV_INTEN may still be armed by
       the previous session. Start from scratch. */
    PV_Write(VC4Base, PV_INTEN, 0);
    PV_Write(VC4Base, PV_INTSTAT, PV_INT_VFP_START);

    bi->HardInterrupt.is_Data = bi;

    if (!VBlank_AddServer(VC4Base, bi, irq))
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
        bug("[VC] VBlank: no interrupt %ld, staying on polling\n", irq);
        VBlank_RemServer(VC4Base, irq, &bi->HardInterrupt);
        return FALSE;
    }

    /* The last step: tell rtg.library */
    VC4Base->vc4_VBlankIrq = irq;
    VC4Base->vc4_VBlankInterrupt = &bi->HardInterrupt;

    bi->SetInterrupt = (void *)VBlank_SetInterrupt;
    bi->Flags |= BIF_VBLANKINTERRUPT;

    /* VC6: GIC id. VC4: bank << 5 | bit of the ARM controller */
    bug("[VC] VBlank: interrupt %ld of the pixelvalve of HDMI0\n", irq);

    return TRUE;
}

void VBlank_Exit(struct VC4Base *VC4Base)
{
    if (VC4Base->vc4_VBlankInterrupt == NULL)
        return;

    PV_Write(VC4Base, PV_INTEN, 0);
    VBlank_RemServer(VC4Base, VC4Base->vc4_VBlankIrq, VC4Base->vc4_VBlankInterrupt);

    VC4Base->vc4_VBlankInterrupt = NULL;
}
