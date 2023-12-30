#ifndef _UNICAM_H
#define _UNICAM_H

#include <exec/types.h>

void unicam_run(ULONG *address , UBYTE lanes, UBYTE datatype, ULONG width , ULONG height , UBYTE bbp,  struct VC4Base * VC4Base);
void unicam_stop (void);
ULONG poll_unicam(ULONG *address);

#endif /* _UNICAM_H */
