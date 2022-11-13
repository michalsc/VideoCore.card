#include "support.h"
#include <stdarg.h>
#include <stdint.h>

#include <proto/exec.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

int _strlen(CONST_STRPTR str)
{
    int len = 0;

    while (*str++) len++;

    return len;
}

struct Task * NewCreateTaskTags(struct TagItem *tags)
{
    struct ExecBase *SysBase = *(struct ExecBase **)4;
    struct Library *UtilityBase = OpenLibrary("utility.library", 0);
    struct Task *task = NULL;

    APTR entry = (APTR)GetTagData(TASKTAG_PC, 0, tags);
    APTR task_name = (APTR)GetTagData(TASKTAG_NAME, (ULONG)"task", tags);
    APTR userdata = (APTR)GetTagData(TASKTAG_USERDATA, 0, tags);
    ULONG task_name_len = _strlen(task_name) + 1;
    UBYTE priority = GetTagData(TASKTAG_PRI, 0, tags);
    ULONG stacksize = GetTagData(TASKTAG_STACKSIZE, 8192, tags);
    ULONG args[] = {
        GetTagData(TASKTAG_ARG1, 0, tags),
        GetTagData(TASKTAG_ARG2, 0, tags),
        GetTagData(TASKTAG_ARG3, 0, tags),
        GetTagData(TASKTAG_ARG4, 0, tags)
    };
    ULONG argcnt = 0;

    if (FindTagItem(TASKTAG_ARG4, tags)) {
        argcnt = 4;
    }
    else if (FindTagItem(TASKTAG_ARG3, tags)) {
        argcnt = 3;
    }
    else if (FindTagItem(TASKTAG_ARG2, tags)) {
        argcnt = 2;
    }
    else if (FindTagItem(TASKTAG_ARG1, tags)) {
        argcnt = 1;
    }

    if (entry != NULL)
    {
        task = AllocMem(sizeof(struct Task), MEMF_PUBLIC | MEMF_CLEAR);
        struct MemList *ml = AllocMem(sizeof(struct MemList) + 2*sizeof(struct MemEntry), MEMF_PUBLIC | MEMF_CLEAR);
        ULONG *stack = AllocMem(stacksize, MEMF_PUBLIC | MEMF_CLEAR);
        ULONG *sp = (ULONG *)((ULONG)stack + stacksize);
        STRPTR name_copy = AllocMem(task_name_len, MEMF_PUBLIC | MEMF_CLEAR);
        
        CopyMem((APTR)task_name, name_copy, task_name_len);

        ml->ml_NumEntries = 3;
        ml->ml_ME[0].me_Un.meu_Addr = task;
        ml->ml_ME[0].me_Length = sizeof(struct Task);

        ml->ml_ME[1].me_Un.meu_Addr = stack;
        ml->ml_ME[1].me_Length = stacksize;

        ml->ml_ME[2].me_Un.meu_Addr = name_copy;
        ml->ml_ME[2].me_Length = task_name_len;

        sp -= argcnt;

        for (int i=0; i < argcnt; i++) {
            sp[i] = args[i];
        }

        task->tc_UserData = userdata;
        task->tc_SPLower = stack;
        task->tc_SPUpper = (APTR)((ULONG)stack + stacksize);
        task->tc_SPReg = sp;

        task->tc_Node.ln_Name = name_copy;
        task->tc_Node.ln_Type = NT_TASK;
        task->tc_Node.ln_Pri = priority;

        NewList(&task->tc_MemEntry);
        AddHead(&task->tc_MemEntry, &ml->ml_Node);

        AddTask(task, entry, NULL);
    }
    
    CloseLibrary(UtilityBase);

    return task;
}