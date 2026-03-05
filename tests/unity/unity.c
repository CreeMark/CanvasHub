#include "unity.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

UnityTestControl Unity;

void UnityBegin(const char* filename)
{
    Unity.TestFile = filename;
    Unity.CurrentTestName = NULL;
    Unity.CurrentTestLineNumber = 0;
    Unity.NumberOfTests = 0;
    Unity.TestFailures = 0;
    Unity.TestIgnores = 0;
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
    // jmp_buf is opaque and platform-dependent, do not manually initialize it.
    // Standard setjmp/longjmp usage doesn't require zeroing it out.
}

int UnityEnd(void)
{
    printf("\n-----------------------\n");
    printf("%lu Tests %lu Failures %lu Ignored\n", Unity.NumberOfTests, Unity.TestFailures, Unity.TestIgnores);
    if (Unity.TestFailures == 0U)
    {
        printf("OK\n");
    }
    else
    {
        printf("FAIL\n");
    }
    return (int)(Unity.TestFailures);
}

void UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, const int FuncLineNum)
{
    Unity.CurrentTestName = FuncName;
    Unity.CurrentTestLineNumber = (unsigned long)FuncLineNum;
    Unity.NumberOfTests++;
    if (setjmp(Unity.AbortFrame) == 0)
    {
        Func();
    }
    if (Unity.CurrentTestFailed)
    {
        // Handle failure (already printed)
    }
}
