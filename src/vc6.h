#ifndef _VC6_H
#define _VC6_H

#include <stdint.h>
#include "boardinfo.h"

void VC6_SetDAC(struct BoardInfo *bi asm("a0"), RGBFTYPE format asm("d7"));
void VC6_SetGC(struct BoardInfo *bi asm("a0"), struct ModeInfo *mode_info asm("a1"), BOOL border asm("d0"));
UWORD VC6_SetSwitch (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled));
void VC6_SetPanning (__REGA0(struct BoardInfo *b), __REGA1(UBYTE *addr), __REGD0(UWORD width), __REGD1(WORD x_offset), __REGD2(WORD y_offset), __REGD7(RGBFTYPE format));
void VC6_SetColorArray (__REGA0(struct BoardInfo *b), __REGD0(UWORD start), __REGD1(UWORD num));
UWORD VC6_CalculateBytesPerRow (__REGA0(struct BoardInfo *b), __REGD0(UWORD width), __REGD7(RGBFTYPE format));
APTR VC6_CalculateMemory (__REGA0(struct BoardInfo *b), __REGA1(unsigned long addr), __REGD7(RGBFTYPE format));
ULONG VC6_GetCompatibleFormats (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
UWORD VC6_SetDisplay (__REGA0(struct BoardInfo *b), __REGD0(UWORD enabled));
LONG VC6_ResolvePixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG pixel_clock), __REGD7(RGBFTYPE format));
ULONG VC6_GetPixelClock (__REGA0(struct BoardInfo *b), __REGA1(struct ModeInfo *mode_info), __REGD0(ULONG index), __REGD7(RGBFTYPE format));
void VC6_SetClock (__REGA0(struct BoardInfo *b));
void VC6_SetMemoryMode (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void VC6_SetWriteMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void VC6_SetClearMask (__REGA0(struct BoardInfo *b), __REGD0(UBYTE mask));
void VC6_SetReadPlane (__REGA0(struct BoardInfo *b), __REGD0(UBYTE plane));
void VC6_WaitVerticalSync (__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle));
BOOL VC6_GetVSyncState(__REGA0(struct BoardInfo *b), __REGD0(BOOL toggle));
void VC6_FillRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(ULONG color), __REGD5(UBYTE mask), __REGD7(RGBFTYPE format));
void VC6_InvertRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void VC6_BlitRect (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE mask), __REGD7(RGBFTYPE format));
void VC6_BlitRectNoMaskComplete (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *rs), __REGA2(struct RenderInfo *rt), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD dx), __REGD3(WORD dy), __REGD4(WORD w), __REGD5(WORD h), __REGD6(UBYTE minterm), __REGD7(RGBFTYPE format));
void VC6_BlitTemplate (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Template *t), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void VC6_BlitPattern (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Pattern *p), __REGD0(WORD x), __REGD1(WORD y), __REGD2(WORD w), __REGD3(WORD h), __REGD4(UBYTE mask), __REGD7(RGBFTYPE format));
void VC6_DrawLine (__REGA0(struct BoardInfo *b), __REGA1(struct RenderInfo *r), __REGA2(struct Line *l), __REGD0(UBYTE mask), __REGD7(RGBFTYPE format));
void VC6_BlitPlanar2Chunky (__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask));
void VC6_BlitPlanar2Direct (__REGA0(struct BoardInfo *b), __REGA1(struct BitMap *bm), __REGA2(struct RenderInfo *r), __REGA3(struct ColorIndexMapping *clut), __REGD0(SHORT x), __REGD1(SHORT y), __REGD2(SHORT dx), __REGD3(SHORT dy), __REGD4(SHORT w), __REGD5(SHORT h), __REGD6(UBYTE minterm), __REGD7(UBYTE mask));
void VC6_SetSprite (__REGA0(struct BoardInfo *b), __REGD0(BOOL enable), __REGD7(RGBFTYPE format));
void VC6_SetSpritePosition (__REGA0(struct BoardInfo *b), __REGD0(WORD x), __REGD1(WORD y), __REGD7(RGBFTYPE format));
void VC6_SetSpriteImage (__REGA0(struct BoardInfo *b), __REGD7(RGBFTYPE format));
void VC6_SetSpriteColor (__REGA0(struct BoardInfo *b), __REGD0(UBYTE idx), __REGD1(UBYTE R), __REGD2(UBYTE G), __REGD3(UBYTE B), __REGD7(RGBFTYPE format));
ULONG VC6_GetVBeamPos(struct BoardInfo *b asm("a0"));

#endif /* _VC6_H */
