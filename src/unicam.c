//#include "support.h"
#include "vc4-regs-unicam.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mbox.h"

#define BIT(n) (UINT32_C(1) << (n))
#define u32 uint32_t
#define AARCH 32
#define GENMASK(h, l) ((~0UL - (1UL << (l)) + 1) & (~0UL >> (AARCH - 1 - (h))))

#define ARM_IO_BASE 0xf2000000
#define ARM_CSI0_BASE (ARM_IO_BASE + 0x800000)
#define ARM_CSI0_END (ARM_CSI0_BASE + 0x7FF)
#define ARM_CSI0_CLKGATE (ARM_IO_BASE + 0x802000) // 4 bytes
#define ARM_CSI1_BASE (ARM_IO_BASE + 0x801000)
#define ARM_CSI1_END (ARM_CSI1_BASE + 0x7FF)
#define ARM_CSI1_CLKGATE (ARM_IO_BASE + 0x802004) // 4 bytes
#define ARM_CM_BASE (ARM_IO_BASE + 0x101000)
#define ARM_CM_CAM0CTL (ARM_CM_BASE + 0x40)
#define ARM_CM_CAM0DIV (ARM_CM_BASE + 0x44)
#define ARM_CM_CAM1CTL (ARM_CM_BASE + 0x48)
#define ARM_CM_CAM1DIV (ARM_CM_BASE + 0x4C)
#define ARM_CM_PASSWD (0x5A << 24)


/*
void myusleep(ULONG us)
{
    ULONG timer = LE32(*(volatile ULONG*)0xf2003004);
    ULONG end = timer + (us * 10);

    if (end < timer) {
        while (end < LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
    }
    while (end > LE32(*(volatile ULONG*)0xf2003004)) asm volatile("nop");
}
*/

void myusleep(ULONG us)
{
    ULONG count;
    for (volatile uint32_t count = us*100; count > 0; count--) {
	asm volatile("nop");asm volatile("nop");asm volatile("nop");
	asm volatile("nop");asm volatile("nop");asm volatile("nop");
	asm volatile("nop");asm volatile("nop");asm volatile("nop");
	asm volatile("nop");asm volatile("nop");asm volatile("nop");
	asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
        asm volatile("nop");asm volatile("nop");asm volatile("nop");
	}

}


void setup_csiclk(void) {

  *(volatile ULONG *)(ARM_CM_CAM1CTL) = LE32(ARM_CM_PASSWD | (1 << 5));
  myusleep(100);
  while ((*(volatile ULONG *)(ARM_CM_CAM1CTL)) & LE32(1 << 7))
    ;
  myusleep(100);
  *(volatile ULONG *)(ARM_CM_CAM1DIV) =
      LE32(ARM_CM_PASSWD | (4 << 12)); // divider , 12=100MHz on pi3 ??
  myusleep(100);
  *(volatile ULONG *)(ARM_CM_CAM1CTL) =
      LE32(ARM_CM_PASSWD | 6 | (1 << 4)); // pll? 6=plld, 5=pllc
  myusleep(100);
  while (((*(volatile ULONG *)(ARM_CM_CAM1CTL)) & LE32(1 << 7)) == 0)
    ;
  myusleep(100);
}

void ClockWrite(u32 nValue) {

  *(volatile ULONG *)(ARM_CSI1_CLKGATE) = LE32(ARM_CM_PASSWD | nValue);
}

void SetField(u32 *pValue, u32 nValue, u32 nMask) {
  u32 nTempMask = nMask;
  while (!(nTempMask & 1)) {
    nValue <<= 1;
    nTempMask >>= 1;
  }

  *pValue = (*pValue & ~nMask) | nValue;
}

u32 ReadReg(u32 nOffset) {
  u32 temp;
  temp = LE32(*(volatile ULONG *)(ARM_CSI1_BASE + nOffset));
  //		printf("ReadReg : %x , %x \n",ARM_CSI1_BASE + nOffset,temp);
  return temp;
}

void WriteReg(u32 nOffset, u32 nValue) {

  //		printf("WriteReg: %x , %x \n",ARM_CSI1_BASE + nOffset,nValue);
  *(volatile ULONG *)(ARM_CSI1_BASE + nOffset) = LE32(nValue);
}

void WriteRegField(u32 nOffset, u32 nValue, u32 nMask) {
  u32 nBuffer = ReadReg(nOffset);
  SetField(&nBuffer, nValue, nMask);
  WriteReg(nOffset, nBuffer);
}

void unicam_run(ULONG *address , UBYTE lanes, UBYTE datatype, ULONG width , ULONG height , UBYTE bbp, struct VC4Base * VC4Base) {

  //enable power domain
  enable_unicam_domain(VC4Base);

  //enable to clock to unicam
  setup_csiclk();

  // Enable lane clocks (2 lanes)
  ClockWrite(0b010101);

  // Basic init
  WriteReg(UNICAM_CTRL, UNICAM_MEM);

  // Enable analogue control, and leave in reset.
  u32 nValue = UNICAM_AR;
  SetField(&nValue, 7, UNICAM_CTATADJ_MASK);
  SetField(&nValue, 7, UNICAM_PTATADJ_MASK);
  WriteReg(UNICAM_ANA, nValue);

  myusleep(1000);

  // Come out of reset
  WriteRegField(UNICAM_ANA, 0, UNICAM_AR);

  // Peripheral reset
  WriteRegField(UNICAM_CTRL, 1, UNICAM_CPR);
  WriteRegField(UNICAM_CTRL, 0, UNICAM_CPR);

  WriteRegField(UNICAM_CTRL, 0, UNICAM_CPE);

  // Enable Rx control (CSI2 DPHY)
  nValue = ReadReg(UNICAM_CTRL);
  SetField(&nValue, UNICAM_CPM_CSI2, UNICAM_CPM_MASK);
  SetField(&nValue, UNICAM_DCM_STROBE, UNICAM_DCM_MASK);

  // Packet framer timeout
  SetField(&nValue, 0xf, UNICAM_PFT_MASK);
  SetField(&nValue, 128, UNICAM_OET_MASK);
  WriteReg(UNICAM_CTRL, nValue);

  WriteReg(UNICAM_IHWIN, 0);
  WriteReg(UNICAM_IVWIN, 0);

  // AXI bus access QoS setup
  nValue = ReadReg(UNICAM_PRI);
  SetField(&nValue, 0, UNICAM_BL_MASK);
  SetField(&nValue, 0, UNICAM_BS_MASK);
  SetField(&nValue, 0xe, UNICAM_PP_MASK);
  SetField(&nValue, 8, UNICAM_NP_MASK);
  SetField(&nValue, 2, UNICAM_PT_MASK);
  SetField(&nValue, 1, UNICAM_PE);
  WriteReg(UNICAM_PRI, nValue);

  WriteRegField(UNICAM_ANA, 0, UNICAM_DDL);

  ULONG nLineIntFreq = height >> 2;
  nValue = UNICAM_FSIE | UNICAM_FEIE | UNICAM_IBOB;
  SetField(&nValue, nLineIntFreq >= 128 ? nLineIntFreq : 128, UNICAM_LCIE_MASK);
  WriteReg(UNICAM_ICTL, nValue);
  WriteReg(UNICAM_STA, UNICAM_STA_MASK_ALL);
  WriteReg(UNICAM_ISTA, UNICAM_ISTA_MASK_ALL);

  WriteRegField(UNICAM_CLT, 2, UNICAM_CLT1_MASK); // tclk_term_en
  WriteRegField(UNICAM_CLT, 6, UNICAM_CLT2_MASK); // tclk_settle
  WriteRegField(UNICAM_DLT, 2, UNICAM_DLT1_MASK); // td_term_en
  WriteRegField(UNICAM_DLT, 6, UNICAM_DLT2_MASK); // ths_settle
  WriteRegField(UNICAM_DLT, 0, UNICAM_DLT3_MASK); // trx_enable

  WriteRegField(UNICAM_CTRL, 0, UNICAM_SOE);

  // Packet compare setup - required to avoid missing frame ends
  nValue = 0;
  SetField(&nValue, 1, UNICAM_PCE);
  SetField(&nValue, 1, UNICAM_GI);
  SetField(&nValue, 1, UNICAM_CPH);
  SetField(&nValue, 0, UNICAM_PCVC_MASK);
  SetField(&nValue, 1, UNICAM_PCDT_MASK);
  WriteReg(UNICAM_CMP0, nValue);

  // Enable clock lane and set up terminations (CSI2 DPHY, non-continous clock)
  nValue = 0;
  SetField(&nValue, 1, UNICAM_CLE);
  SetField(&nValue, 1, UNICAM_CLLPE);
  WriteReg(UNICAM_CLK, nValue);

  // Enable required data lanes with appropriate terminations.
  // The same value needs to be written to UNICAM_DATn registers for
  // the active lanes, and 0 for inactive ones.
  // (CSI2 DPHY, non-continous clock, 2 data lanes)
  nValue = 0;
  SetField(&nValue, 1, UNICAM_DLE);
  SetField(&nValue, 1, UNICAM_DLLPE);
  WriteReg(UNICAM_DAT0, nValue);
  if (lanes == 1)
   WriteReg(UNICAM_DAT1, 0);
  if (lanes == 2)
   WriteReg(UNICAM_DAT1, nValue);

  WriteReg(UNICAM_IBLS, width*(bbp/8));

  // Write DMA buffer address

  WriteReg(UNICAM_IBSA0, (u32)(address) & ~0xC0000000 | 0xC0000000);
  WriteReg(UNICAM_IBEA0,
           (u32)(address + (width * height * (bbp/8))) & ~0xC0000000 | 0xC0000000);

  // Set packing configuration
  u32 nUnPack = UNICAM_PUM_NONE;
  u32 nPack = UNICAM_PPM_NONE;

  nValue = 0;
  SetField(&nValue, nUnPack, UNICAM_PUM_MASK);
  SetField(&nValue, nPack, UNICAM_PPM_MASK);
  WriteReg(UNICAM_IPIPE, nValue);

  // CSI2 mode, hardcode VC 0 for now.
  WriteReg(UNICAM_IDI0, (0 << 6) | datatype);

  nValue = ReadReg(UNICAM_MISC);
  SetField(&nValue, 1, UNICAM_FL0);
  SetField(&nValue, 1, UNICAM_FL1);
  WriteReg(UNICAM_MISC, nValue);

  // Clear ED setup
  WriteReg(UNICAM_DCS, 0);

  // Enable peripheral
  WriteRegField(UNICAM_CTRL, 1, UNICAM_CPE);

  // Load image pointers
  WriteRegField(UNICAM_ICTL, 1, UNICAM_LIP_MASK);
}

void unicam_stop(void) {

  // Analogue lane control disable
  WriteRegField(UNICAM_ANA, 1, UNICAM_DDL);

  // Stop the output engine
  WriteRegField(UNICAM_CTRL, 1, UNICAM_SOE);

  // Disable the data lanes
  WriteReg(UNICAM_DAT0, 0);
  WriteReg(UNICAM_DAT1, 0);

  // Peripheral reset
  WriteRegField(UNICAM_CTRL, 1, UNICAM_CPR);
  myusleep(50);
  WriteRegField(UNICAM_CTRL, 0, UNICAM_CPR);

  // Disable peripheral
  WriteRegField(UNICAM_CTRL, 0, UNICAM_CPE);

  // Disable all lane clocks
  ClockWrite(0);

  //Disable unicam power domain
// TODO  disable_unicam_domain();

}

// TODO this can be IRQ..
ULONG poll_unicam(ULONG *address) {

  ULONG ret=0;
  u32 nSTA = ReadReg(UNICAM_STA);
  WriteReg(UNICAM_STA, nSTA); // Write value back to clear the interrupts

  u32 nISTA = ReadReg(UNICAM_ISTA);
  WriteReg(UNICAM_ISTA, nISTA); // Write value back to clear the interrupts

  //Update DMA pointers
/*
          WriteReg (UNICAM_IBSA0,(u32)(&address) | 0xC0000000);
          WriteReg (UNICAM_IBEA0,(u32)(&address+(width*height*(bbp/8))) | 0xC0000000);
      //    printf("STA : %x \n",nSTA);
      //    printf("ISTA: %x \n",nISTA);
*/
        if ((nISTA & UNICAM_FEI) || (nSTA & UNICAM_PI0))
        {
//           printf("Frame End IRQ\n");
	   ret = 1;
        }


        if (nISTA & UNICAM_FSI)
        {
//           printf("Frame Start IRQ\n");
        }
return ret;

}
