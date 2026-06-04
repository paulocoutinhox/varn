#pragma once

// installs a last-resort handler so a native crash prints a diagnosis to stderr instead of dying
// silently. only the windows path does anything; every other platform already surfaces a usable
// signal/backtrace through its own tooling, so there it is a no-op.

#if defined(_WIN32)

#include <windows.h>
// dbghelp.h must follow windows.h.
#include <dbghelp.h>

#include <cstdint>
#include <cstdio>

namespace varn::diagnostics {

inline const char* exceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
            return "access violation";
        case EXCEPTION_STACK_OVERFLOW:
            return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            return "integer divide by zero";
        case EXCEPTION_PRIV_INSTRUCTION:
            return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR:
            return "in-page i/o error";
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return "datatype misalignment";
        case 0xC0000374:
            return "heap corruption";
        case 0xC0000409:
            return "stack buffer overrun";
        default:
            return "unknown exception";
    }
}

inline void printBacktrace() {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    if (!SymInitialize(process, nullptr, TRUE)) {
        return;
    }

    void* frames[62];
    const USHORT count = CaptureStackBackTrace(0, 62, frames, nullptr);

    char storage[sizeof(SYMBOL_INFO) + 256] = {0};
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;

    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 address = static_cast<DWORD64>(reinterpret_cast<uintptr_t>(frames[i]));
        DWORD64 symbolOffset = 0;

        if (SymFromAddr(process, address, &symbolOffset, symbol)) {
            IMAGEHLP_LINE64 line = {0};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineOffset = 0;

            if (SymGetLineFromAddr64(process, address, &lineOffset, &line)) {
                fprintf(stderr, "  #%2u %s + 0x%llx  (%s:%lu)\n", i, symbol->Name,
                        static_cast<unsigned long long>(symbolOffset), line.FileName,
                        static_cast<unsigned long>(line.LineNumber));
            } else {
                fprintf(stderr, "  #%2u %s + 0x%llx\n", i, symbol->Name,
                        static_cast<unsigned long long>(symbolOffset));
            }
        } else {
            fprintf(stderr, "  #%2u %p\n", i, frames[i]);
        }
    }

    SymCleanup(process);
}

inline LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const DWORD code = record->ExceptionCode;

    // emit and flush the essentials first, so they survive even if the stack walk below faults.
    fprintf(stderr, "\n[crash] unhandled exception 0x%08lX (%s) at %p\n", static_cast<unsigned long>(code),
            exceptionName(code), record->ExceptionAddress);

    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const ULONG_PTR operation = record->ExceptionInformation[0];
        const char* verb = operation == 0 ? "reading" : (operation == 1 ? "writing" : "executing");
        fprintf(stderr, "[crash] while %s address 0x%llx\n", verb,
                static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    fflush(stderr);

    // a stack overflow runs the handler on a nearly empty stack, so a symbolized walk would fault
    // again; the summary above is enough to identify it.
    if (code != EXCEPTION_STACK_OVERFLOW) {
        printBacktrace();
        fflush(stderr);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

inline void installCrashHandler() {
    // keep a stack reserve so the handler can still run and report on a stack-overflow exception.
    ULONG reserve = 65536;
    SetThreadStackGuarantee(&reserve);
    SetUnhandledExceptionFilter(&crashFilter);
}

} // namespace varn::diagnostics

#else

namespace varn::diagnostics {

inline void installCrashHandler() {}

} // namespace varn::diagnostics

#endif
