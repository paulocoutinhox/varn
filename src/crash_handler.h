#pragma once

// installs last-resort handlers so a native crash prints a diagnosis to stderr instead of dying silently, wiring std::set_terminate on every platform to catch an unhandled c++ exception (including one escaping a worker thread) and adding a windows structured-exception filter with a symbolized backtrace for faults the terminate handler never sees, such as an access violation.

#include <cstdio>
#include <cstdlib>
#include <exception>

#if defined(_WIN32)

#include <windows.h>
// dbghelp.h must follow windows.h.
#include <dbghelp.h>

#include <cstdint>

#endif

namespace varn::diagnostics {

class CrashHandler {
public:
    CrashHandler() = delete;

    static void install();

private:
#if defined(_WIN32)
    static const char* exceptionName(DWORD code);
    static void printBacktrace();
    static LONG WINAPI crashFilter(EXCEPTION_POINTERS* info);
#endif

    static void printActiveException();
    static void terminateHandler();
};

#if defined(_WIN32)

inline const char* CrashHandler::exceptionName(DWORD code) {
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
            return "stack buffer overrun or fast-fail";
        default:
            return "unknown exception";
    }
}

inline void CrashHandler::printBacktrace() {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    if (!SymInitialize(process, nullptr, TRUE)) {
        return;
    }

    // capture only the innermost frames: the fault site is near the top, and a short trace fits inside the tail the test runner echoes on failure.
    void* frames[30];
    const USHORT count = CaptureStackBackTrace(0, 30, frames, nullptr);

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

inline LONG WINAPI CrashHandler::crashFilter(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const DWORD code = record->ExceptionCode;

    // emit and flush the essentials first, so they survive even if the stack walk below faults.
    fprintf(stderr, "\n[CrashHandler] Unhandled exception 0x%08lX (%s) at %p.\n", static_cast<unsigned long>(code),
            exceptionName(code), record->ExceptionAddress);

    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const ULONG_PTR operation = record->ExceptionInformation[0];
        const char* verb = operation == 0 ? "reading" : (operation == 1 ? "writing" : "executing");
        fprintf(stderr, "[CrashHandler] While %s address 0x%llx.\n", verb,
                static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    fflush(stderr);

    // a stack overflow runs the handler on a nearly empty stack where a symbolized walk would fault again, so the summary above is enough to identify it.
    if (code != EXCEPTION_STACK_OVERFLOW) {
        printBacktrace();
        fflush(stderr);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

#endif // _WIN32

inline void CrashHandler::printActiveException() {
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& ex) {
            fprintf(stderr, "[CrashHandler] Unhandled C++ exception: %s.\n", ex.what());
        } catch (...) {
            fprintf(stderr, "[CrashHandler] Unhandled non-standard C++ exception.\n");
        }
    } else {
        fprintf(stderr, "[CrashHandler] Terminate with no active exception (often a longjmp across C++ frames).\n");
    }
}

inline void CrashHandler::terminateHandler() {
    fprintf(stderr, "\n[CrashHandler] Triggered by std::terminate.\n");
    printActiveException();
    fflush(stderr);

#if defined(_WIN32)
    printBacktrace();
    fflush(stderr);
#endif

    std::abort();
}

inline void CrashHandler::install() {
    std::set_terminate(&CrashHandler::terminateHandler);

#if defined(_WIN32)
    // keep a stack reserve so the filter can still run and report on a stack-overflow exception.
    ULONG reserve = 65536;
    SetThreadStackGuarantee(&reserve);
    SetUnhandledExceptionFilter(&CrashHandler::crashFilter);
#endif
}

} // namespace varn::diagnostics
