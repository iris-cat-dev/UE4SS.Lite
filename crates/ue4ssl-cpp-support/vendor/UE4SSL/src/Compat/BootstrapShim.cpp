#define NOMINMAX

#include <Windows.h>
#include <cstdio>
#include <exception>
#include <limits>
#include <tlhelp32.h>

#include <Compat/BootstrapShim.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <UE4SSProgram.hpp>

namespace RC::Compat::Bootstrap
{
    auto thread_dll_start(UE4SSProgram* program) -> unsigned long
    {
        try
        {
            program->init();

            if (auto e = program->get_error_object(); e->has_error())
            {
                if (!Output::has_internal_error())
                {
                    Output::send<LogLevel::Error>(STR("Fatal Error: {}\n"), ensure_str(e->get_message()));
                }
                else
                {
                    printf_s("Error: %s\n", e->get_message());
                }
            }
        }
        catch (const std::exception& e)
        {
            try
            {
                if (!Output::has_internal_error())
                {
                    Output::send<LogLevel::Error>(STR("[UE4SS] Unhandled exception in thread_dll_start: {}\n"), ensure_str(e.what()));
                }
                else
                {
                    printf_s("[UE4SS] Unhandled exception in thread_dll_start: %s\n", e.what());
                }
            }
            catch (...) {}
        }
        catch (...)
        {
            try
            {
                if (!Output::has_internal_error())
                {
                    Output::send<LogLevel::Error>(STR("[UE4SS] Unknown exception in thread_dll_start\n"));
                }
                else
                {
                    printf_s("[UE4SS] Unknown exception in thread_dll_start\n");
                }
            }
            catch (...) {}
        }

        return 0;
    }

    static void process_initialized_impl(HMODULE module_handle)
    {
        try
        {
            wchar_t module_filename_buffer[1024]{'\0'};
            GetModuleFileNameW(module_handle, module_filename_buffer, sizeof(module_filename_buffer) / sizeof(wchar_t));

            auto program = new UE4SSProgram(module_filename_buffer, {});
            if (HANDLE handle = CreateThread(
                        nullptr,
                        0,
                        reinterpret_cast<LPTHREAD_START_ROUTINE>(thread_dll_start),
                        static_cast<LPVOID>(program),
                        0,
                        nullptr);
                handle)
            {
                CloseHandle(handle);
            }
        }
        catch (const std::exception& e)
        {
            printf_s("[UE4SS] Exception in process_initialized: %s\n", e.what());
        }
        catch (...)
        {
            printf_s("[UE4SS] Unknown exception in process_initialized\n");
        }
    }

    auto process_initialized(HMODULE module_handle) -> void
    {
        __try
        {
            process_initialized_impl(module_handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            printf_s("[UE4SS] SEH exception in process_initialized: 0x%08lX\n", GetExceptionCode());
        }
    }

    static auto get_main_thread_id() -> DWORD
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return 0;
        }

        DWORD current_pid = GetCurrentProcessId();

        THREADENTRY32 th32;
        th32.dwSize = sizeof(THREADENTRY32);

        uint64_t earliest_creation_time = std::numeric_limits<uint64_t>::max();
        DWORD main_thread_id = 0;

        for (Thread32First(snapshot, &th32); Thread32Next(snapshot, &th32);)
        {
            if (th32.th32OwnerProcessID != current_pid)
            {
                continue;
            }

            HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, false, th32.th32ThreadID);
            if (!thread)
            {
                continue;
            }

            FILETIME thread_times[4];
            if (!GetThreadTimes(thread, &thread_times[0], &thread_times[1], &thread_times[2], &thread_times[3]))
            {
                CloseHandle(thread);
                continue;
            }

            uint64_t creation_time = (static_cast<uint64_t>(thread_times[0].dwHighDateTime) << 32) | thread_times[0].dwLowDateTime;
            if (creation_time < earliest_creation_time)
            {
                earliest_creation_time = creation_time;
                main_thread_id = th32.th32ThreadID;
            }

            CloseHandle(thread);
        }

        CloseHandle(snapshot);
        return main_thread_id;
    }

    auto dll_process_attached(HMODULE module_handle) -> void
    {
        if (GetCurrentThreadId() == get_main_thread_id())
        {
            QueueUserAPC(reinterpret_cast<PAPCFUNC>(reinterpret_cast<void*>(&process_initialized)), GetCurrentThread(), reinterpret_cast<ULONG_PTR>(module_handle));
        }
        else
        {
            process_initialized(module_handle);
        }
    }

    auto static_cleanup_seh() -> void
    {
        __try
        {
            UE4SSProgram::static_cleanup();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
} // namespace RC::Compat::Bootstrap
