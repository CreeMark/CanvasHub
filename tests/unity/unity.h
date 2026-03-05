/*
 *  Unity - A Test Framework for C
 *  Copyright (c) 2007-14 Mike Karlesky, Mark VanderVoord, Greg Williams
 *  MIT License 
 */

#ifndef UNITY_FRAMEWORK_H
#define UNITY_FRAMEWORK_H

#define UNITY_VERSION_MAJOR    2
#define UNITY_VERSION_MINOR    5
#define UNITY_VERSION_BUILD    2
#define UNITY_VERSION          ((UNITY_VERSION_MAJOR << 16) | (UNITY_VERSION_MINOR << 8) | UNITY_VERSION_BUILD)

#ifdef __cplusplus
extern "C"
{
#endif

#include <setjmp.h>
#include <math.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------
 * Configuration Options
 * -------------------------------------- */

#define UNITY_OUTPUT_CHAR(a) putchar(a)
#define UNITY_OUTPUT_FLUSH() fflush(stdout)
#define UNITY_PRINT_EOL()    UNITY_OUTPUT_CHAR('\n')

/* --------------------------------------
 * Test Control and Statistics
 * -------------------------------------- */

typedef void (*UnityTestFunction)(void);

typedef struct {
    const char* TestFile;
    const char* CurrentTestName;
    unsigned long CurrentTestLineNumber;
    unsigned long NumberOfTests;
    unsigned long TestFailures;
    unsigned long TestIgnores;
    unsigned long CurrentTestFailed;
    unsigned long CurrentTestIgnored;
    jmp_buf AbortFrame;
} UnityTestControl;

extern UnityTestControl Unity;

void UnityBegin(const char* filename);
int  UnityEnd(void);
void UnityDefaultTestRun(UnityTestFunction Func, const char* FuncName, const int FuncLineNum);

#define UNITY_BEGIN() UnityBegin(__FILE__)
#define UNITY_END() UnityEnd()

/* --------------------------------------
 * Assertion Macros
 * -------------------------------------- */

#define TEST_FAIL_MESSAGE(message) \
    { Unity.TestFailures++; Unity.CurrentTestFailed = 1; \
    printf("%s:%lu:FAIL: %s\n", Unity.TestFile, Unity.CurrentTestLineNumber, (message)); \
    longjmp(Unity.AbortFrame, 1); }

#define TEST_ASSERT(condition) \
    if (!(condition)) { TEST_FAIL_MESSAGE("Expression Evaluated to FALSE"); }

#define TEST_ASSERT_TRUE(condition) TEST_ASSERT(condition)
#define TEST_ASSERT_FALSE(condition) TEST_ASSERT(!(condition))

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    if ((expected) != (actual)) { Unity.TestFailures++; Unity.CurrentTestFailed = 1; \
    printf("%s:%lu:FAIL: Expected %d Was %d\n", Unity.TestFile, Unity.CurrentTestLineNumber, (int)(expected), (int)(actual)); \
    longjmp(Unity.AbortFrame, 1); }

#define TEST_ASSERT_EQUAL(expected, actual) TEST_ASSERT_EQUAL_INT(expected, actual)

#define TEST_ASSERT_EQUAL_HEX32(expected, actual) \
    if ((expected) != (actual)) { Unity.TestFailures++; Unity.CurrentTestFailed = 1; \
    printf("%s:%lu:FAIL: Expected 0x%08X Was 0x%08X\n", Unity.TestFile, Unity.CurrentTestLineNumber, (unsigned int)(expected), (unsigned int)(actual)); \
    longjmp(Unity.AbortFrame, 1); }

#define TEST_ASSERT_EQUAL_DOUBLE(expected, actual) \
    if (fabs((expected) - (actual)) > 0.00001) { Unity.TestFailures++; Unity.CurrentTestFailed = 1; \
    printf("%s:%lu:FAIL: Expected %f Was %f\n", Unity.TestFile, Unity.CurrentTestLineNumber, (double)(expected), (double)(actual)); \
    longjmp(Unity.AbortFrame, 1); }

#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    if (strcmp((expected), (actual)) != 0) { Unity.TestFailures++; Unity.CurrentTestFailed = 1; \
    printf("%s:%lu:FAIL: Expected '%s' Was '%s'\n", Unity.TestFile, Unity.CurrentTestLineNumber, (expected), (actual)); \
    longjmp(Unity.AbortFrame, 1); }

#define TEST_ASSERT_NOT_NULL(pointer) \
    if ((pointer) == NULL) { TEST_FAIL_MESSAGE("Expected Non-NULL Pointer"); }

#define TEST_ASSERT_NULL(pointer) \
    if ((pointer) != NULL) { TEST_FAIL_MESSAGE("Expected NULL Pointer"); }

/* Simplified Run Test Macro */
#define RUN_TEST(func) UnityDefaultTestRun(func, #func, __LINE__)

#ifdef __cplusplus
}
#endif

#endif /* UNITY_FRAMEWORK_H */
