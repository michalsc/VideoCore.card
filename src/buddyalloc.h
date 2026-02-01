#ifndef SRC_BUDDYALLOC_H_
#define SRC_BUDDYALLOC_H_

#include "emu68-vc4.h"

void BuddyInit(struct VC4Base *base);
ULONG BuddyAlloc(struct VC4Base *base, UWORD size);
void BuddyFree(struct VC4Base *base, ULONG id);

#define BUDDY_SIZE(x) ((UWORD)((x) >> 16))
#define BUDDY_OFFSET(x)  ((UWORD)((x) & 0xffff))

#endif // SRC_BUDDYALLOC_H_
