#include <exception>
#include <utility>
#include <DynamicOutput/Output.hpp>
#if defined(_WIN32) && defined(_MSC_VER)
#define NOMINMAX
#include <Windows.h>
#endif

namespace RC::Output
{
    namespace
    {
        thread_local std::exception_ptr* dispatch_error{};

        auto operate_device(void* data, const uint16_t* text, size_t length, int32_t level, int operation) -> bool
        {
#if defined(_WIN32) && defined(_MSC_VER)
            __try
            {
#endif
                auto* device = static_cast<OutputDevice*>(data);
                if (operation == 1) { delete device; }
                else if (operation == 2) { device->lock(); }
                else if (operation == 3) { device->unlock(); }
                else if (device->has_optional_arg())
                {
                    device->receive_with_optional_arg(length == 0 ? RC::StringViewType{} : RC::StringViewType{reinterpret_cast<const RC::CharType*>(text), length}, level);
                }
                else
                {
                    device->receive(length == 0 ? RC::StringViewType{} : RC::StringViewType{reinterpret_cast<const RC::CharType*>(text), length});
                }
                return true;
#if defined(_WIN32) && defined(_MSC_VER)
            }
            __except (GetExceptionCode() == 0xE06D7363 ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
#endif
        }

        auto receive(void* data, const uint16_t* text, size_t length, int32_t level) noexcept -> uint8_t
        {
            try
            {
                if (!operate_device(data, text, length, level, 0)) { throw std::runtime_error{"Structured exception in output device"}; }
                return 1;
            }
            catch (...)
            {
                if (dispatch_error) { *dispatch_error = std::current_exception(); }
                return 0;
            }
        }
        auto release(void* data) noexcept -> void
        {
            try { if (!operate_device(data, nullptr, 0, 0, 1)) { Internal::internal_error = true; } }
            catch (...) { Internal::internal_error = true; }
        }
        auto lock_default_devices(bool locking) -> void
        {
            struct Context { bool locking; std::exception_ptr error; } context{locking, {}};
            ue4ssl_native_log_group_find(DefaultTargets::get_group(), &context, [](void* data, void* raw_context) noexcept -> void* {
                auto& context = *static_cast<Context*>(raw_context);
                try
                {
                    if (!operate_device(data, nullptr, 0, 0, context.locking ? 2 : 3)) { throw std::runtime_error{"Structured exception locking output device"}; }
                }
                catch (...) { context.error = std::current_exception(); }
                return nullptr;
            });
            if (context.error) { std::rethrow_exception(context.error); }
        }
    }

    namespace detail
    {
        auto add_device(uint64_t group, std::unique_ptr<OutputDevice> device) -> OutputDevice&
        {
            if (!device || !ue4ssl_native_log_group_add(group, device.get(), receive, release))
            {
                THROW_INTERNAL_OUTPUT_ERROR("[Output::add_device] Failed to register output device")
            }
            return *device.release();
        }
        auto send_to_group(uint64_t group, RC::StringViewType content, int32_t level) -> void
        {
            static_assert(sizeof(RC::CharType) == sizeof(uint16_t));
            std::exception_ptr error;
            auto* previous = std::exchange(dispatch_error, &error);
            const auto success = ue4ssl_native_log_group_send(group, reinterpret_cast<const uint16_t*>(content.data()), content.size(), level);
            dispatch_error = previous;
            if (error) { std::rethrow_exception(error); }
            if (!success)
            {
                THROW_INTERNAL_OUTPUT_ERROR("[Output::send] No output devices are open or an output device failed")
            }
        }
    }

    auto has_internal_error() -> bool { return Internal::internal_error; }
    auto DefaultTargets::get_group() -> uint64_t
    {
        struct Cleanup
        {
            ~Cleanup() { ue4ssl_native_log_group_clear(ue4ssl_native_log_default_group()); }
        };
        static Cleanup cleanup;
        return ue4ssl_native_log_default_group();
    }
    auto DefaultTargets::set_default_log_level(int32_t level) -> void { ue4ssl_native_log_set_default_level(level); }
    auto DefaultTargets::get_default_log_level() -> int32_t { return ue4ssl_native_log_get_default_level(); }
    auto DefaultTargets::close_all_default_devices() -> void
    {
        if (!ue4ssl_native_log_group_clear(get_group()))
        {
            THROW_INTERNAL_OUTPUT_ERROR("[Output::close] Failed to close output devices")
        }
    }
    auto send(RC::StringViewType content) -> void { detail::send_to_group(DefaultTargets::get_group(), content, 0); }
    auto close_all_default_devices() -> void { DefaultTargets::close_all_default_devices(); }

    Lock::Lock() { lock_default_devices(true); }
    Lock::Lock(const OutputDevice* device) : m_output_device(device)
    {
        if (device) { device->lock(); }
    }
    Lock::~Lock()
    {
        try
        {
            if (m_output_device) { m_output_device->unlock(); }
            else { lock_default_devices(false); }
        }
        catch (...) { Internal::internal_error = true; }
    }
}
