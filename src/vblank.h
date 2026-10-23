#ifndef _VBLANK_H
#define _VBLANK_H

#include <exec/types.h>
#include "boardinfo.h"
#include "emu68-vc4.h"

/*
    Vertical blank interrupt for Picasso96 (BIF_VBLANKINTERRUPT, SetInterrupt, HardInterrupt), taken from the
    pixelvalve of HDMI0: through the GIC-400 and gic400.library on the VC6, through the ARM interrupt controller
    and INTB_EXTER on the VC4. With it rtg.library lets WaitBOVP(), WaitTOF() and the double buffering sleep
    until the blank instead of calling WaitVerticalSync(), which spins.

    VBlank_Init() returns TRUE when the interrupt is set up. Anything missing (a VC4 without the BCM2836/2837
    controller, no gic400.library or a too old one, no interrupt in the device tree, no interrupt seen) leaves
    the BoardInfo as it is and returns FALSE.
*/
BOOL VBlank_Init(struct BoardInfo *bi);
void VBlank_Exit(struct VC4Base *VC4Base);

/*
    Writes the position word of the sprite plane (vc4_MouseCoord[0]). The HVS draws a shifted copy of the sprite
    for a frame when that word changes in the middle of one, so with the interrupt the word waits for the next
    vertical blank and is written by the handler, the latest one wins. Without the interrupt it is written at once.
*/
void VBlank_WriteSprite(struct VC4Base *VC4Base, ULONG word);

/* Forgets a word which waits: called when vc4_MouseCoord moves to a new display list, which gets the current
   position of the sprite anyway */
static inline void VBlank_DropSprite(struct VC4Base *VC4Base)
{
    VC4Base->vc4_SpritePending = FALSE;
}

#endif /* _VBLANK_H */
