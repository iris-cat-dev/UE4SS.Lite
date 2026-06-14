#include <CrashDumper.hpp>
#include <string>
#include <format>
#include <bit>
#include <UE4SSProgram.hpp>
#include <Unreal/Core/Windows/WindowsHWrapper.hpp>

#include <polyhook2/PE/IatHook.hpp>
#include <dbghelp.h>

#include <String/StringType.hpp>

namespace fs = std::filesystem;

using std::chrono::seconds;
using std::chrono::system_clock;
using std::chrono::time_point_cast;

namespace RC
{
    const int DumpType =
            MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithModuleHeaders | MiniDumpWithAvxXStateContext;

    static bool FullMemoryDump = false;

    static UE4SSProgram* seh_get_program_ptr()
    {
        __try
        {
            return &UE4SSProgram::get_program();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    LONG WINAPI ExceptionHandler(_EXCEPTION_POINTERS* exception_pointers)
    {
        const auto now = time_point_cast<seconds>(system_clock::now());

        StringType working_dir;
        try
        {
            auto* prog = seh_get_program_ptr();
            if (prog)
            {
                working_dir = prog->get_working_directory();
            }
        }
        catch (...) {}

        if (working_dir.empty())
        {
            wchar_t fallback[MAX_PATH]{};
            GetModuleFileNameW(nullptr, fallback, MAX_PATH);
            std::filesystem::path p(fallback);
            working_dir = ensure_str(p.parent_path());
        }

        StringType dump_path;
        try
        {
            dump_path = std::format(STR("{}\\crash_{:%Y_%m_%d_%H_%M_%S}.dmp"), working_dir, now);
        }
        catch (...)
        {
            MessageBoxW(NULL, L"Failed to format crashdump path.", L"Fatal Error!", MB_OK);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const HANDLE file = CreateFileW(FromCharTypePtr<wchar_t>(dump_path.c_str()), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

        if (file == INVALID_HANDLE_VALUE)
        {
            const StringType message = std::format(STR("Failed to create crashdump file, reason: {:x}"), GetLastError());
            MessageBoxW(NULL, FromCharTypePtr<wchar_t>(message.c_str()), L"Fatal Error!", MB_OK);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        _MINIDUMP_EXCEPTION_INFORMATION exception_information{};
        exception_information.ThreadId = GetCurrentThreadId();
        exception_information.ExceptionPointers = exception_pointers;
        exception_information.ClientPointers = NULL;

        const int additional_dump_flags = FullMemoryDump ? MiniDumpWithFullMemory | MiniDumpIgnoreInaccessibleMemory : 0;
        bool ok = MiniDumpWriteDump(GetCurrentProcess(),
                                    GetCurrentProcessId(),
                                    file,
                                    static_cast<MINIDUMP_TYPE>(DumpType | additional_dump_flags),
                                    &exception_information,
                                    NULL,
                                    NULL);
        CloseHandle(file);

        if (!ok)
        {
            const StringType message = std::format(STR("Failed to write crashdump file, reason: {:x}"), GetLastError());
            MessageBoxW(NULL, FromCharTypePtr<wchar_t>(message.c_str()), L"Fatal Error!", MB_OK);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        StringType message = std::format(STR("Crashdump written to: {}"), dump_path);
        MessageBoxW(NULL, FromCharTypePtr<wchar_t>(message.c_str()), L"Fatal Error!", MB_OK);

        return EXCEPTION_EXECUTE_HANDLER;
    }

    LPTOP_LEVEL_EXCEPTION_FILTER WINAPI HookedSetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter)
    {
        return nullptr;
    }

    CrashDumper::CrashDumper()
    {
    }

    CrashDumper::~CrashDumper()
    {
        try
        {
            if (enabled && m_set_unhandled_exception_filter_hook)
            {
                m_set_unhandled_exception_filter_hook->unHook();
            }
            if (m_previous_exception_filter)
            {
                SetUnhandledExceptionFilter(reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(m_previous_exception_filter));
            }
        }
        catch (...) {}
    }

    void CrashDumper::enable()
    {
        SetErrorMode(SEM_FAILCRITICALERRORS);
        m_previous_exception_filter = SetUnhandledExceptionFilter(ExceptionHandler);

        m_set_unhandled_exception_filter_hook = std::make_unique<PLH::IatHook>("kernel32.dll",
                                                                               "SetUnhandledExceptionFilter",
                                                                               std::bit_cast<uint64_t>(&HookedSetUnhandledExceptionFilter),
                                                                               &m_hook_trampoline_set_unhandled_exception_filter_hook,
                                                                               L"");
        m_set_unhandled_exception_filter_hook->hook();
        this->enabled = true;
    }

    void CrashDumper::set_full_memory_dump(bool enabled)
    {
        FullMemoryDump = enabled;
    }

} // namespace RC
