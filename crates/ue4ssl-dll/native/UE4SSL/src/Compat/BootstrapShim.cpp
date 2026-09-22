#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <exception>
#include <UE4SSProgram.hpp>
#include <SehFramework.hpp>

namespace
{
    void* create_program_cpp(const uint16_t* path) noexcept
    {
        try { return new RC::UE4SSProgram(std::filesystem::path{reinterpret_cast<const wchar_t*>(path)}, {}); }
        catch (const std::exception& error) { std::fprintf(stderr, "[UE4SSL] Program construction: %s\n", error.what()); }
        catch (...) { std::fprintf(stderr, "[UE4SSL] Program construction: unknown C++ exception\n"); }
        return nullptr;
    }
    void run_program_cpp(void* ptr) noexcept
    {
        try
        {
            auto* program = static_cast<RC::UE4SSProgram*>(ptr);
            program->init();
            if (auto error = program->get_error_object(); error->has_error())
                std::fprintf(stderr, "[UE4SSL] Program initialization: %s\n", error->get_message());
        }
        catch (const std::exception& error) { std::fprintf(stderr, "[UE4SSL] Program initialization: %s\n", error.what()); }
        catch (...) { std::fprintf(stderr, "[UE4SSL] Program initialization: unknown C++ exception\n"); }
    }
    void destroy_program_cpp(void* ptr) noexcept
    {
        try { delete static_cast<RC::UE4SSProgram*>(ptr); }
        catch (...) { std::fprintf(stderr, "[UE4SSL] Program destruction failed\n"); }
    }
}
extern "C"
{
    void* ue4ssl_native_program_create(const uint16_t* path)
    {
        __try { return create_program_cpp(path); }
        __except (RC::Seh::FilterAndLog(L"Bootstrap", L"create", GetExceptionCode(), GetExceptionInformation())) { return nullptr; }
    }
    // init enters Rust runtime: no surrounding SEH catch may unwind Rust frames.
    void ue4ssl_native_program_run(void* ptr) { run_program_cpp(ptr); }
    void ue4ssl_native_program_destroy(void* ptr)
    {
        __try { destroy_program_cpp(ptr); }
        __except (RC::Seh::FilterAndLog(L"Bootstrap", L"destroy", GetExceptionCode(), GetExceptionInformation())) {}
    }
}
