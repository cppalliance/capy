//
// Copyright (c) 2019 Vinnie Falco (vinnie.falco@gmail.com)
// Copyright (c) 2022 Alan de Freitas (alandefreitas@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/cppalliance/capy
//

#include "test_suite.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace {

[[noreturn]] void
on_terminate() noexcept
{
    std::fprintf(stderr,
        "\n"
        "======================================================\n"
        "FATAL: std::terminate() called\n"
        "======================================================\n");

    if(auto ep = std::current_exception())
    {
        try
        {
            std::rethrow_exception(ep);
        }
        catch(std::exception const& e)
        {
            std::fprintf(stderr,
                "Exception: %s\n", e.what());
        }
        catch(...)
        {
            std::fprintf(stderr,
                "Unknown exception type\n");
        }
    }
    else
    {
        std::fprintf(stderr,
            "No active exception (likely noexcept violation)\n");
    }

    std::fprintf(stderr,
        "======================================================\n");
    std::fflush(stderr);
    std::abort();
}

} // namespace

#ifdef _WIN32
#include <windows.h>
#include <crtdbg.h>

namespace {

const char*
seh_code_string(DWORD code) noexcept
{
    switch(code)
    {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:       return "FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
    case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    case 0xE06D7363:                         return "C++ Exception";
    default:                                 return "UNKNOWN";
    }
}

LONG WINAPI
seh_exception_handler(EXCEPTION_POINTERS* info) noexcept
{
    DWORD code = info->ExceptionRecord->ExceptionCode;

    // C++ exceptions that escaped - less alarming output
    if(code == 0xE06D7363)
    {
        std::fprintf(stderr, "An unhandled C++ exception occurred\n");
        std::fflush(stderr);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void* addr = info->ExceptionRecord->ExceptionAddress;

    std::fprintf(stderr,
        "\n"
        "======================================================\n"
        "FATAL: Windows Structured Exception (crash detected)\n"
        "======================================================\n"
        "Exception Code: 0x%08lX (%s)\n"
        "Fault Address:  %p\n"
        "------------------------------------------------------\n"
        "This is a native crash, not a C++ exception.\n"
        "Common causes: null pointer dereference, buffer overrun,\n"
        "stack overflow, or use-after-free.\n"
        "======================================================\n",
        code, seh_code_string(code), addr);
    std::fflush(stderr);

    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace
#endif

int
main(int argc, char const* const* argv)
{
    // Read build timestamp from companion .build_time file written
    // as a POST_BUILD step. The file sits next to the executable.
    {
        char path[4096];
        std::snprintf(path, sizeof(path), "%s.build_time", argv[0]);
        std::FILE* f = std::fopen(path, "r");
        if (f)
        {
            char buf[64];
            if (std::fgets(buf, sizeof(buf), f))
            {
                char* nl = std::strchr(buf, '\n');
                if (nl)
                    *nl = '\0';
                std::fprintf(stdout, "Built: %s\n", buf);
            }
            std::fclose(f);
        }
    }
    std::set_terminate(on_terminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(seh_exception_handler);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    // Leak check runs after all static destructors
    _CrtSetDbgFlag(
        _CRTDBG_ALLOC_MEM_DF |
        _CRTDBG_LEAK_CHECK_DF);
#endif
#endif
    int result = ::test_suite::run(argc, argv);
    if(result != 0)
        std::fprintf(stderr, "test_suite::run returned %d!\n", result);
    return result;
}
