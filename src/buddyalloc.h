#ifndef SRC_BUDDYALLOC_H_
#define SRC_BUDDYALLOC_H_

#include "emu68-vc4.h"

void BuddyInit(struct VC4Base *base);
int BuddyAlloc(struct VC4Base *base, UWORD size);
void BuddyFree(struct VC4Base *base, UWORD offset, UWORD size);

#endif // SRC_BUDDYALLOC_H_
