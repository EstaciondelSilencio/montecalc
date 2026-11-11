// MonteCalc - the parts of the Microsoft Excel C API (Excel 2007 and later, XLOPER12) used by
// the XLL, written from the published documentation of the API.
#pragma once

#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define XLL_EXPORT extern "C" __declspec(dllexport)
#define XLL_CALL __stdcall
typedef wchar_t XCHAR;
#else   // test builds on other systems (tests/xll_test.cpp drives the add-in through a mock Excel)
#define XLL_EXPORT extern "C" __attribute__((visibility("default")))
#define XLL_CALL
typedef char16_t XCHAR;
typedef int BOOL;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint8_t BYTE;
typedef uintptr_t DWORD_PTR;
#endif

typedef int32_t RW;
typedef int32_t COL;
typedef DWORD_PTR IDSHEET;

struct XLREF12 {
    RW rwFirst, rwLast;
    COL colFirst, colLast;
};

struct XLMREF12 {
    WORD count;
    XLREF12 reftbl[1];   // actually reftbl[count]
};

struct XLOPER12 {
    union {
        double num;
        XCHAR* str;   // length-prefixed: str[0] = length
        BOOL xbool;
        int err;
        int w;
        struct {
            WORD count;
            XLREF12 ref;
        } sref;
        struct {
            XLMREF12* lpmref;
            IDSHEET idSheet;
        } mref;
        struct {
            XLOPER12* lparray;
            RW rows;
            COL columns;
        } array;
        struct {
            union {
                int level;
                int tbctrl;
                IDSHEET idSheet;
            } valflow;
            RW rw;
            COL col;
            BYTE xlflow;
        } flow;
        struct {
            union {
                BYTE* lpbData;
                void* hdata;
            } h;
            long cbData;
        } bigdata;
    } val;
    DWORD xltype;
};
typedef XLOPER12* LPXLOPER12;

// xltype
constexpr DWORD xltypeNum = 0x0001, xltypeStr = 0x0002, xltypeBool = 0x0004, xltypeRef = 0x0008, xltypeErr = 0x0010,
                xltypeFlow = 0x0020, xltypeMulti = 0x0040, xltypeMissing = 0x0080, xltypeNil = 0x0100, xltypeSRef = 0x0400,
                xltypeInt = 0x0800, xlbitXLFree = 0x1000, xlbitDLLFree = 0x4000;
// errors
constexpr int xlerrNull = 0, xlerrDiv0 = 7, xlerrValue = 15, xlerrRef = 23, xlerrName = 29, xlerrNum = 36, xlerrNA = 42;
// return codes
constexpr int xlretSuccess = 0, xlretAbort = 1, xlretInvXlfn = 2, xlretInvCount = 4, xlretInvXloper = 8, xlretStackOvfl = 16,
              xlretFailed = 32, xlretUncalced = 64;
// function numbers
constexpr int xlCommand = 0x8000, xlSpecial = 0x4000;
constexpr int xlFree = 0 | xlSpecial, xlCoerce = 2 | xlSpecial, xlSet = 3 | xlSpecial, xlSheetId = 4 | xlSpecial, xlSheetNm = 5 | xlSpecial,
              xlAbort = 6 | xlSpecial, xlGetName = 9 | xlSpecial;
constexpr int xlfCaller = 89, xlfActiveCell = 94, xlfInput = 104, xlfGetName = 107, xlfRegister = 149, xlfAddMenu = 152,
              xlfDeleteMenu = 158, xlfGetBar = 182, xlfGetCell = 185, xlfUnregister = 201, xlfEvaluate = 257, xlfGetFormula = 106;
constexpr int xlcCalculateNow = 31 | xlCommand, xlcDefineName = 61 | xlCommand, xlcFormula = 96 | xlCommand, xlcAlert = 118 | xlCommand,
              xlcMessage = 122 | xlCommand, xlcEcho = 141 | xlCommand;

// Excel12v: the call back into Excel (MdCallBack12 in the Excel process)
int Excel12v(int xlfn, LPXLOPER12 operRes, int count, LPXLOPER12 opers[]);
